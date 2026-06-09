#pragma once
/*
 * sinks/smf78_3_parquet_sink.h — Parquet sink for SMF type 78 s3 (I/O Queuing).
 * Multi-row: one output row per LCU.
 */

#include "smf/smf78_reader.h"   // smf::Smf78Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf78_3Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    lcu_id;
    std::shared_ptr<arrow::UInt8Builder>     css_id;
    std::shared_ptr<arrow::UInt32Builder>    queue_len_sum;
    std::shared_ptr<arrow::UInt32Builder>    queue_len_count;

    explicit Smf78_3Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms    (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name   (std::make_shared<arrow::StringBuilder>(pool)),
          lcu_id         (std::make_shared<arrow::StringBuilder>(pool)),
          css_id         (std::make_shared<arrow::UInt8Builder>(pool)),
          queue_len_sum  (std::make_shared<arrow::UInt32Builder>(pool)),
          queue_len_count(std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",     arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",    arrow::utf8()));
        f.push_back(arrow::field("lcu_id",          arrow::utf8()));
        f.push_back(arrow::field("css_id",          arrow::uint8()));
        f.push_back(arrow::field("queue_len_sum",   arrow::uint32()));
        f.push_back(arrow::field("queue_len_count", arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf78Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.lcus.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const auto ims = r.product.interval_hund * 10u;
        const auto& sn = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);
        char hex[5];
        for (const auto& lcu : r.lcus) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms    ->Append(ims));
            arrow_ok(sysplex_name   ->Append(sn));
            std::snprintf(hex, sizeof hex, "%04X", lcu.lcu_id);
            arrow_ok(lcu_id         ->Append(hex));
            arrow_ok(css_id         ->Append(lcu.css_id));
            arrow_ok(queue_len_sum  ->Append(lcu.queue_len_sum));
            arrow_ok(queue_len_count->Append(lcu.queue_len_count));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(lcu_id.get()));
        c.push_back(fin(css_id.get()));            c.push_back(fin(queue_len_sum.get()));
        c.push_back(fin(queue_len_count.get()));
        return c;
    }
};

class Smf78_3ParquetSink {
public:
    Smf78_3ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf78-3", std::move(customer), Smf78_3Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf78Record& r) {
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
    PartitionedTable<Smf78_3Builders> table_;
};

} // namespace s2p
