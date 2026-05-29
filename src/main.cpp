/*
 * SMF2Parquet — split a downloaded SMF log into one Parquet table per record type.
 *
 * Memory-maps a RECFM=VB SMF file, runs mf::process_smf_file, dispatches on the
 * record type byte, and (when built with the Parquet sink layer) writes one
 * Hive-partitioned Parquet table per type via the shared smf:: parse cores:
 *
 *   <out_dir>/smf_type=<NN>/smf_date=YYYY-MM-DD/<table>-<run_id>.parquet
 *
 * Types 30/70/74/75 are fully parsed; 71/72/73/76/77/78/79/99/113 currently
 * capture the common header columns; any other type lands losslessly in `raw`.
 *
 * Build without Arrow (parsing + per-type counts only):
 *   cmake -B build && cmake --build build
 *   smf2parquet <input.smf>
 *
 * Build with the Parquet sink layer (requires libarrow-dev/libparquet-dev):
 *   cmake -B build -DSMF2PARQUET_WITH_PARQUET=ON && cmake --build build
 *   smf2parquet <input.smf> <out_dir>
 *
 * Exit codes: 0 success · 1 usage error · 2 I/O / parse error
 */

#include "smf_file_reader.h"   // mf::process_smf_file, mf::SmfHeader, mf::Reader

#include "smf/smf30_reader.h"
#include "smf/smf70_reader.h"
#include "smf/smf71_reader.h"
#include "smf/smf72_reader.h"
#include "smf/smf73_reader.h"
#include "smf/smf74_reader.h"
#include "smf/smf75_reader.h"
#include "smf/smf76_reader.h"
#include "smf/smf77_reader.h"
#include "smf/smf78_reader.h"
#include "smf/smf79_reader.h"
#include "smf/smf99_reader.h"
#include "smf/smf113_reader.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef SMF2PARQUET_WITH_PARQUET
#include "sinks/smf30_parquet_sink.h"
#include "sinks/smf70_parquet_sink.h"
#include "sinks/smf74_parquet_sink.h"
#include "sinks/smf75_parquet_sink.h"
#include "sinks/smf99_parquet_sink.h"
#include "sinks/header_only_sink.h"
#include "sinks/raw_parquet_sink.h"
#include <chrono>
#include <ctime>
#endif

namespace {

/* ── Memory-mapped input file (POSIX / WSL) ──────────────────────────────── */
struct MmapFile {
    const std::byte* data{nullptr};
    std::size_t      size{0};
    int              fd{-1};

    explicit MmapFile(const char* path) {
        fd = ::open(path, O_RDONLY);
        if (fd < 0)
            throw std::runtime_error(std::string("open: ") + std::strerror(errno));
        struct stat st{};
        if (::fstat(fd, &st) < 0)
            throw std::runtime_error(std::string("fstat: ") + std::strerror(errno));
        size = static_cast<std::size_t>(st.st_size);
        if (size > 0) {
            void* p = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED)
                throw std::runtime_error(std::string("mmap: ") + std::strerror(errno));
            data = static_cast<const std::byte*>(p);
            ::madvise(const_cast<std::byte*>(data), size, MADV_SEQUENTIAL);
        }
    }
    ~MmapFile() {
        if (data && size > 0) ::munmap(const_cast<std::byte*>(data), size);
        if (fd >= 0)          ::close(fd);
    }
    MmapFile(const MmapFile&)            = delete;
    MmapFile& operator=(const MmapFile&) = delete;
};

#ifdef SMF2PARQUET_WITH_PARQUET
/* ── All per-type Parquet sinks for one run ──────────────────────────────── */
struct SinkSet {
    s2p::Smf30ParquetSink s30;
    s2p::Smf70ParquetSink s70;
    s2p::Smf74ParquetSink s74;
    s2p::Smf75ParquetSink s75;
    s2p::Smf99ParquetSink s99;
    s2p::RawParquetSink    raw;
    s2p::HeaderOnlySink s71, s72, s73, s76, s77, s78, s79, s113;

    SinkSet(const std::string& out, const std::string& run,
            const std::string& src, int64_t ing)
        : s30(out, run, src, ing), s70(out, run, src, ing),
          s74(out, run, src, ing), s75(out, run, src, ing),
          s99(out, run, src, ing), raw(out, run, src, ing),
          s71 ("smf71",  out, run, src, ing), s72 ("smf72",  out, run, src, ing),
          s73 ("smf73",  out, run, src, ing), s76 ("smf76",  out, run, src, ing),
          s77 ("smf77",  out, run, src, ing), s78 ("smf78",  out, run, src, ing),
          s79 ("smf79",  out, run, src, ing), s113("smf113", out, run, src, ing) {}

    void close() {
        s30.close(); s70.close(); s71.close(); s72.close(); s73.close();
        s74.close(); s75.close(); s76.close(); s77.close(); s78.close();
        s79.close(); s99.close(); s113.close(); raw.close();
    }

    void report() const {
        auto line = [](const char* name, uint64_t rows) {
            if (rows) std::printf("  %-6s : %llu rows\n", name,
                                  static_cast<unsigned long long>(rows));
        };
        std::printf("Parquet tables written:\n");
        line("smf30", s30.rows());  line("smf70", s70.rows());  line("smf71", s71.rows());
        line("smf72", s72.rows());  line("smf73", s73.rows());  line("smf74", s74.rows());
        line("smf75", s75.rows());  line("smf76", s76.rows());  line("smf77", s77.rows());
        line("smf78", s78.rows());  line("smf79", s79.rows());  line("smf99", s99.rows());
        line("smf113", s113.rows()); line("raw", raw.rows());
    }
};
#endif

/* ── Dispatcher: count by type; route to the matching sink ───────────────── */
struct Dispatcher {
    std::array<std::uint64_t, 256> by_type{};
    std::uint64_t total{0};
#ifdef SMF2PARQUET_WITH_PARQUET
    SinkSet* sinks{nullptr};
#endif

    void on_record(const mf::SmfHeader& hdr, mf::Reader& r) {
        ++total;
        ++by_type[hdr.record_type];
#ifdef SMF2PARQUET_WITH_PARQUET
        if (!sinks) return;
        const auto span = r.data;
        switch (hdr.record_type) {
            case 30:  sinks->s30.write(smf::read_smf30(span)); break;
            case 70:  sinks->s70.write(smf::read_smf70(span)); break;
            case 71:  { auto rec = smf::read_smf71(span);  sinks->s71.write(rec.header, 0); } break;
            case 72:  { auto rec = smf::read_smf72(span);  sinks->s72.write(rec.header, rec.subtype); } break;
            case 73:  { auto rec = smf::read_smf73(span);  sinks->s73.write(rec.header, 0); } break;
            case 74:  sinks->s74.write(smf::read_smf74(span)); break;
            case 75:  sinks->s75.write(smf::read_smf75(span)); break;
            case 76:  { auto rec = smf::read_smf76(span);  sinks->s76.write(rec.header, rec.subtype); } break;
            case 77:  { auto rec = smf::read_smf77(span);  sinks->s77.write(rec.header, rec.subtype); } break;
            case 78:  { auto rec = smf::read_smf78(span);  sinks->s78.write(rec.header, rec.subtype); } break;
            case 79:  { auto rec = smf::read_smf79(span);  sinks->s79.write(rec.header, 0); } break;
            case 99:  sinks->s99.write(smf::read_smf99(span)); break;
            case 113: { auto rec = smf::read_smf113(span); sinks->s113.write(rec.header, rec.subtype); } break;
            default:  sinks->raw.write(hdr, span); break;
        }
#endif
    }
};

void report_counts(const Dispatcher& d, const char* input) {
    std::printf("SMF2Parquet — %s\n", input);
    std::printf("Total records: %llu\n", static_cast<unsigned long long>(d.total));
    std::printf("Per record type:\n");
    for (int t = 0; t < 256; ++t)
        if (d.by_type[t])
            std::printf("  type %3d : %llu\n", t,
                        static_cast<unsigned long long>(d.by_type[t]));
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef SMF2PARQUET_WITH_PARQUET
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <input.smf> <out_dir>\n", argv[0]);
        return 1;
    }
    const char* out_dir = argv[2];
#else
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <input.smf>\n", argv[0]);
        return 1;
    }
#endif
    const char* input = argv[1];

    try {
        MmapFile in{input};
        Dispatcher d;

#ifdef SMF2PARQUET_WITH_PARQUET
        const int64_t ingest_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        char tsbuf[32];
        const std::time_t tt = static_cast<std::time_t>(ingest_us / 1'000'000);
        std::tm tmv{};
        ::gmtime_r(&tt, &tmv);
        std::strftime(tsbuf, sizeof tsbuf, "%Y%m%dT%H%M%SZ", &tmv);
        const std::string run_id = std::string(tsbuf) + "-" + std::to_string(::getpid());

        SinkSet sinks{out_dir, run_id, input, ingest_us};
        d.sinks = &sinks;

        if (in.size > 0) mf::process_smf_file({in.data, in.size}, d);
        sinks.close();

        report_counts(d, input);
        sinks.report();
        std::printf("Output: %s  (run_id=%s)\n", out_dir, run_id.c_str());
#else
        if (in.size > 0) mf::process_smf_file({in.data, in.size}, d);
        report_counts(d, input);
#endif
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 2;
    }
}
