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
#include <map>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef SMF2PARQUET_WITH_PARQUET
#include "sinks/smf30_parquet_sink.h"
#include "sinks/smf70_1_parquet_sink.h"
#include "sinks/smf70_2_parquet_sink.h"
#include "sinks/smf71_1_parquet_sink.h"
#include "sinks/smf72_3_parquet_sink.h"
#include "sinks/smf73_1_parquet_sink.h"
#include "sinks/smf74_1_parquet_sink.h"
#include "sinks/smf74_2_path_parquet_sink.h"
#include "sinks/smf74_2_member_parquet_sink.h"
#include "sinks/smf74_3_parquet_sink.h"
#include "sinks/smf74_4_parquet_sink.h"
#include "sinks/smf74_5_parquet_sink.h"
#include "sinks/smf74_6_parquet_sink.h"
#include "sinks/smf74_7_parquet_sink.h"
#include "sinks/smf74_8_parquet_sink.h"
#include "sinks/smf74_9_parquet_sink.h"
#include "sinks/smf75_1_parquet_sink.h"
#include "sinks/smf76_1_parquet_sink.h"
#include "sinks/smf77_1_parquet_sink.h"
#include "sinks/smf78_3_parquet_sink.h"
#include "sinks/smf79_1_parquet_sink.h"
#include "sinks/smf79_2_parquet_sink.h"
#include "sinks/smf79_13_parquet_sink.h"
#include "sinks/smf98_parquet_sink.h"
#include "sinks/smf1154_1_parquet_sink.h"
#include "sinks/smf1154_2_parquet_sink.h"
#include "sinks/smf1154_83_parquet_sink.h"
#include "sinks/smf113_parquet_sink.h"
#include "sinks/subtype_router_sink.h"
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
    s2p::Smf30ParquetSink   s30;
    s2p::Smf70_1ParquetSink s70_1;
    s2p::Smf70_2ParquetSink s70_2;
    s2p::Smf71_1ParquetSink s71_1;
    s2p::Smf72_3ParquetSink s72_3;
    s2p::Smf73_1ParquetSink s73_1;
    s2p::Smf74_1ParquetSink s74_1;
    s2p::Smf74_2_PathParquetSink s74_2_path;
    s2p::Smf74_2_MemberParquetSink s74_2_member;
    s2p::Smf74_3ParquetSink s74_3;
    s2p::Smf74_4ParquetSink s74_4;
    s2p::Smf74_5ParquetSink s74_5;
    s2p::Smf74_6ParquetSink s74_6;
    s2p::Smf74_7ParquetSink s74_7;
    s2p::Smf74_8ParquetSink s74_8;
    s2p::Smf74_9ParquetSink s74_9;
    s2p::Smf75_1ParquetSink s75_1;
    s2p::Smf76_1ParquetSink s76_1;
    s2p::Smf77_1ParquetSink s77_1;
    s2p::Smf78_3ParquetSink s78_3;
    s2p::Smf79_1ParquetSink s79_1;
    s2p::Smf79_2ParquetSink s79_2;
    s2p::Smf79_13ParquetSink s79_13;
    s2p::Smf98ParquetSink  s98;
    s2p::Smf1154_1ParquetSink s1154_1;
    s2p::Smf1154_2ParquetSink s1154_2;
    s2p::Smf1154_83ParquetSink s1154_83;
    s2p::Smf113ParquetSink s113;
    s2p::RawParquetSink    raw;
    s2p::SubtypeRouterSink s70, s71, s72, s73, s74, s75, s76, s77, s78, s79, s99, s113_other, s1154_other;

    SinkSet(const std::string& cust, const std::string& out, const std::string& run,
            const std::string& src, int64_t ing)
        : s30(cust, out, run, src, ing), s70_1(cust, out, run, src, ing),
          s70_2(cust, out, run, src, ing),
          s71_1(cust, out, run, src, ing), s72_3(cust, out, run, src, ing),
          s73_1(cust, out, run, src, ing), s74_1(cust, out, run, src, ing),
          s74_2_path(cust, out, run, src, ing),
          s74_2_member(cust, out, run, src, ing),
          s74_3(cust, out, run, src, ing),
          s74_4(cust, out, run, src, ing),
          s74_5(cust, out, run, src, ing),
          s74_6(cust, out, run, src, ing),
          s74_7(cust, out, run, src, ing),
          s74_8(cust, out, run, src, ing),
          s74_9(cust, out, run, src, ing),
          s75_1(cust, out, run, src, ing),
          s76_1(cust, out, run, src, ing),
          s77_1(cust, out, run, src, ing),
          s78_3(cust, out, run, src, ing),
          s79_1(cust, out, run, src, ing),
          s79_2(cust, out, run, src, ing),
          s79_13(cust, out, run, src, ing),
          s98(cust, out, run, src, ing),
          s1154_1(cust, out, run, src, ing),
          s1154_2(cust, out, run, src, ing),
          s1154_83(cust, out, run, src, ing),
          s113(cust, out, run, src, ing), raw(cust, out, run, src, ing),
          s70 ("smf70",  cust, out, run, src, ing), s71 ("smf71",  cust, out, run, src, ing),
          s72 ("smf72",  cust, out, run, src, ing), s73 ("smf73",  cust, out, run, src, ing),
          s74 ("smf74",  cust, out, run, src, ing), s75 ("smf75",  cust, out, run, src, ing),
          s76 ("smf76",  cust, out, run, src, ing), s77 ("smf77",  cust, out, run, src, ing),
          s78 ("smf78",  cust, out, run, src, ing), s79 ("smf79",  cust, out, run, src, ing),
          s99 ("smf99",  cust, out, run, src, ing), s113_other("smf113", cust, out, run, src, ing),
          s1154_other("smf1154", cust, out, run, src, ing) {}

    void close() {
        s30.close(); s70_1.close(); s70_2.close(); s70.close(); s71_1.close(); s71.close(); s72_3.close(); s72.close();
        s73_1.close(); s73.close(); s74_1.close();
        s74_2_path.close(); s74_2_member.close(); s74_3.close();
        s74_4.close(); s74_5.close(); s74_6.close(); s74_7.close(); s74_8.close(); s74_9.close(); s74.close();
        s75_1.close(); s75.close(); s76_1.close(); s76.close(); s77_1.close(); s77.close(); s78_3.close(); s78.close();
        s79_1.close(); s79_2.close(); s79_13.close(); s79.close(); s98.close(); 
        s1154_1.close(); s1154_2.close(); s1154_83.close(); s1154_other.close();
        s99.close(); s113.close(); s113_other.close(); raw.close();
    }

    void report() const {
        auto line = [](const char* name, uint64_t rows) {
            if (rows) std::printf("  %-6s : %llu rows\n", name,
                                  static_cast<unsigned long long>(rows));
        };
        std::printf("Parquet tables written:\n");
        line("smf30", s30.rows());
        line("smf70-1", s70_1.rows()); line("smf70-2", s70_2.rows()); line("smf70-other", s70.rows());
        line("smf71-1", s71_1.rows()); line("smf71-other", s71.rows());
        line("smf72-3", s72_3.rows()); line("smf72-other", s72.rows());
        line("smf73-1", s73_1.rows()); line("smf73-other", s73.rows());
        line("smf74-1", s74_1.rows()); 
        line("smf74-2p", s74_2_path.rows()); line("smf74-2m", s74_2_member.rows());
        line("smf74-3", s74_3.rows());
        line("smf74-4", s74_4.rows()); line("smf74-5", s74_5.rows()); 
        line("smf74-6", s74_6.rows()); line("smf74-7", s74_7.rows());
        line("smf74-8", s74_8.rows()); line("smf74-9", s74_9.rows()); line("smf74-other", s74.rows());
        line("smf75-1", s75_1.rows()); line("smf75-other", s75.rows());
        line("smf76-1", s76_1.rows()); line("smf76-other", s76.rows());
        line("smf77-1", s77_1.rows());
        line("smf76", s76.rows());     line("smf77-other", s77.rows());
        line("smf78-3", s78_3.rows()); line("smf78-other", s78.rows());
        line("smf79-1", s79_1.rows()); line("smf79-2", s79_2.rows()); line("smf79-13", s79_13.rows()); line("smf79-other", s79.rows());
        line("smf98", s98.rows());     
        line("smf1154-1", s1154_1.rows()); line("smf1154-2", s1154_2.rows()); 
        line("smf1154-83", s1154_83.rows()); line("smf1154-other", s1154_other.rows());
        line("smf99", s99.rows());
        line("smf113", s113.rows());   line("smf113-other", s113_other.rows());
        line("raw", raw.rows());
    }
};
#endif

/* ── Dispatcher: count by type; route to the matching sink ───────────────── */
struct Dispatcher {
    std::map<std::uint16_t, std::uint64_t> by_type;
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
        const uint16_t subtype = mf::peek_subtype(r);
        switch (hdr.record_type) {
            case 30:  sinks->s30.write(smf::read_smf30(span)); break;
            case 70:  {
                if (subtype == 1) sinks->s70_1.write(smf::read_smf70(span));
                else if (subtype == 2) sinks->s70_2.write(smf::read_smf70_2(span));
                else sinks->s70.write(hdr, subtype);
            } break;
            case 71:  {
                if (subtype == 1) sinks->s71_1.write(smf::read_smf71(span));
                else sinks->s71.write(hdr, subtype);
            } break;
            case 72:  {
                if (subtype == 3) sinks->s72_3.write(smf::read_smf72(span));
                else sinks->s72.write(hdr, subtype);
            } break;
            case 73:  {
                if (subtype == 1) sinks->s73_1.write(smf::read_smf73(span));
                else sinks->s73.write(hdr, subtype);
            } break;
            case 74:  {
                if (subtype == 1) {
                    sinks->s74_1.write(smf::read_smf74(span));
                } else if (subtype == 2) {
                    auto rec = smf::read_smf74_2(span);
                    sinks->s74_2_path.write(rec);
                    sinks->s74_2_member.write(rec);
                } else if (subtype == 3) {
                    sinks->s74_3.write(smf::read_smf74_3(span));
                } else if (subtype == 4) {
                    sinks->s74_4.write(smf::read_smf74_4(span));
                } else if (subtype == 5) {
                    sinks->s74_5.write(smf::read_smf74_5(span));
                } else if (subtype == 6) {
                    sinks->s74_6.write(smf::read_smf74_6(span));
                } else if (subtype == 7) {
                    sinks->s74_7.write(smf::read_smf74_7(span));
                } else if (subtype == 8) {
                    sinks->s74_8.write(smf::read_smf74_8(span));
                } else if (subtype == 9) {
                    sinks->s74_9.write(smf::read_smf74_9(span));
                } else {
                    sinks->s74.write(hdr, subtype);
                }
            } break;
            case 75:  {
                if (subtype == 1) sinks->s75_1.write(smf::read_smf75(span));
                else sinks->s75.write(hdr, subtype);
            } break;
            case 76:  {
                if (subtype == 1) sinks->s76_1.write(smf::read_smf76(span));
                else sinks->s76.write(hdr, subtype);
            } break;
            case 77:  {
                if (subtype == 1) sinks->s77_1.write(smf::read_smf77(span));
                else sinks->s77.write(hdr, subtype);
            } break;
            case 78:  {
                if (subtype == 3) sinks->s78_3.write(smf::read_smf78(span));
                else sinks->s78.write(hdr, subtype);
            } break;
            case 79:  {
                if (subtype == 1) {
                    sinks->s79_1.write(smf::read_smf79(span));
                } else if (subtype == 2) {
                    sinks->s79_2.write(smf::read_smf79(span));
                } else if (subtype == 13) {
                    sinks->s79_13.write(smf::read_smf79(span));
                } else {
                    sinks->s79.write(hdr, subtype);
                }
            } break;
            case 98:  sinks->s98.write(smf::read_smf98(span)); break;
            case 99:  sinks->s99.write(hdr, subtype); break;
            case 1154: {
                auto rec = smf::read_smf1154(span);
                if (subtype == 1)      sinks->s1154_1.write(rec);
                else if (subtype == 2)  sinks->s1154_2.write(rec);
                else if (subtype == 83) sinks->s1154_83.write(rec);
                else                   sinks->s1154_other.write(hdr, subtype);
            } break;
            case 113: {
                if (subtype == 1 || subtype == 2) sinks->s113.write(smf::read_smf113(span));
                else sinks->s113_other.write(hdr, subtype);
            } break;
            default:  sinks->raw.write(hdr, span); break;
        }
#endif
    }
};

void report_counts(const Dispatcher& d, const char* input) {
    std::printf("SMF2Parquet — %s\n", input);
    std::printf("Total records: %llu\n", static_cast<unsigned long long>(d.total));
    std::printf("Per record type:\n");
    for (const auto& [type, count] : d.by_type)
        std::printf("  type %3d : %llu\n", static_cast<int>(type),
                    static_cast<unsigned long long>(count));
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef SMF2PARQUET_WITH_PARQUET
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "Usage: %s <input.smf> <out_dir> [customer]\n", argv[0]);
        return 1;
    }
    const char* out_dir = argv[2];
    const std::string customer = (argc == 4) ? argv[3] : "INTERNAL";
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

        SinkSet sinks{customer, out_dir, run_id, input, ingest_us};
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
