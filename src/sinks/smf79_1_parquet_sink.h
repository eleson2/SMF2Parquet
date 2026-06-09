#pragma once
/*
 * sinks/smf79_1_parquet_sink.h — Parquet sink for SMF type 79 s1 (AS State).
 * Multi-row: one output row per address space.
 */

#include "smf/smf79_reader.h"   // smf::Smf79Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf79_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt16Builder>    asid;
    std::shared_ptr<arrow::StringBuilder>    job_name;
    std::shared_ptr<arrow::UInt32Builder>    total_cpu_ms;
    std::shared_ptr<arrow::UInt32Builder>    real_frames;
    std::shared_ptr<arrow::StringBuilder>    service_class;
    std::shared_ptr<arrow::UInt32Builder>    ziip_ms;

    explicit Smf79_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          asid          (std::make_shared<arrow::UInt16Builder>(pool)),
          job_name      (std::make_shared<arrow::StringBuilder>(pool)),
          total_cpu_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          real_frames   (std::make_shared<arrow::UInt32Builder>(pool)),
          service_class (std::make_shared<arrow::StringBuilder>(pool)),
          ziip_ms       (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("asid",           arrow::uint16()));
        f.push_back(arrow::field("job_name",       arrow::utf8()));
        f.push_back(arrow::field("total_cpu_ms",   arrow::uint32()));
        f.push_back(arrow::field("real_frames",    arrow::uint32()));
        f.push_back(arrow::field("service_class",  arrow::utf8()));
        f.push_back(arrow::field("ziip_ms",        arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf79Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.as_states.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const auto ims = r.product.interval_hund * 10u;
        const auto& sn = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);
        for (const auto& s : r.as_states) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(ims));
            arrow_ok(sysplex_name ->Append(sn));
            arrow_ok(asid         ->Append(s.asid));
            arrow_ok(job_name     ->Append(s.job_name));
            arrow_ok(total_cpu_ms ->Append(s.total_cpu_ms));
            arrow_ok(real_frames  ->Append(s.real_frames));
            arrow_ok(service_class->Append(s.service_class));
            arrow_ok(ziip_ms      ->Append(s.ziip_ms));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(asid.get()));
        c.push_back(fin(job_name.get()));          c.push_back(fin(total_cpu_ms.get()));
        c.push_back(fin(real_frames.get()));       c.push_back(fin(service_class.get()));
        c.push_back(fin(ziip_ms.get()));
        return c;
    }
};

class Smf79_1ParquetSink {
public:
    Smf79_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf79-1", std::move(customer), Smf79_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf79Record& r) {
        const auto day = CommonColumns::partition_day(r.header);
        const auto& sys = r.header.system_id;
        auto p = table_.get_partition(sys, day);
        const uint64_t n = p.builders->append(r, source_, ingest_);
        table_.added(p, n);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<Smf79_1Builders> table_;
};

} // namespace s2p
