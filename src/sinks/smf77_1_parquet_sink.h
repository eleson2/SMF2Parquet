#pragma once
/*
 * sinks/smf77_1_parquet_sink.h — Parquet sink for SMF type 77 s1 (Enqueue Activity).
 * Multi-row: one output row per enqueue resource in contention.
 */

#include "smf/smf77_reader.h"   // smf::Smf77Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf77_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::UInt16Builder>    sample_count;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    major_name;
    std::shared_ptr<arrow::StringBuilder>    minor_name;
    std::shared_ptr<arrow::UInt32Builder>    total_wait_ms;
    std::shared_ptr<arrow::UInt32Builder>    max_wait_ms;

    explicit Smf77_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          sample_count (std::make_shared<arrow::UInt16Builder>(pool)),
          sysplex_name (std::make_shared<arrow::StringBuilder>(pool)),
          major_name   (std::make_shared<arrow::StringBuilder>(pool)),
          minor_name   (std::make_shared<arrow::StringBuilder>(pool)),
          total_wait_ms(std::make_shared<arrow::UInt32Builder>(pool)),
          max_wait_ms  (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",   arrow::uint32()));
        f.push_back(arrow::field("sample_count",  arrow::uint16()));
        f.push_back(arrow::field("sysplex_name",  arrow::utf8()));
        f.push_back(arrow::field("major_name",    arrow::utf8()));
        f.push_back(arrow::field("minor_name",    arrow::utf8()));
        f.push_back(arrow::field("total_wait_ms", arrow::uint32()));
        f.push_back(arrow::field("max_wait_ms",   arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf77Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& enq : r.enqueues) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(r.product.interval_hund * 10u));
            arrow_ok(sample_count ->Append(r.product.sample_count));
            arrow_ok(sysplex_name ->Append(r.product.sysplex_name));
            arrow_ok(major_name   ->Append(enq.major_name));
            arrow_ok(minor_name   ->Append(enq.minor_name));
            arrow_ok(total_wait_ms->Append(enq.total_wait_ms));
            arrow_ok(max_wait_ms  ->Append(enq.max_wait_ms));
        }
        return r.enqueues.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sample_count.get()));      c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(major_name.get()));        c.push_back(fin(minor_name.get()));
        c.push_back(fin(total_wait_ms.get()));     c.push_back(fin(max_wait_ms.get()));
        return c;
    }
};

class Smf77_1ParquetSink {
public:
    Smf77_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf77-1", std::move(customer), Smf77_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf77Record& r) {
        const auto day = CommonColumns::partition_day(r.header);
        const auto& sys = r.header.system_id;
        const uint64_t n = table_.partition(sys, day).append(r, source_, ingest_);
        table_.added(sys, day, n);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<Smf77_1Builders> table_;
};

} // namespace s2p
