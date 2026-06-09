#pragma once
/*
 * sinks/smf71_1_parquet_sink.h — Parquet sink for SMF type 71 s1 (RMF Paging Activity).
 */

#include "smf/smf71_reader.h"   // smf::Smf71Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf71_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    total_page_ins;
    std::shared_ptr<arrow::UInt32Builder>    total_page_outs;
    std::shared_ptr<arrow::UInt32Builder>    avg_avail_frames;
    std::shared_ptr<arrow::UInt32Builder>    max_avail_frames;
    std::shared_ptr<arrow::UInt32Builder>    min_avail_frames;
    std::shared_ptr<arrow::UInt32Builder>    fixed_frames;

    explicit Smf71_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms      (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name     (std::make_shared<arrow::StringBuilder>(pool)),
          total_page_ins   (std::make_shared<arrow::UInt32Builder>(pool)),
          total_page_outs  (std::make_shared<arrow::UInt32Builder>(pool)),
          avg_avail_frames (std::make_shared<arrow::UInt32Builder>(pool)),
          max_avail_frames (std::make_shared<arrow::UInt32Builder>(pool)),
          min_avail_frames (std::make_shared<arrow::UInt32Builder>(pool)),
          fixed_frames     (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",       arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",      arrow::utf8()));
        f.push_back(arrow::field("total_page_ins",    arrow::uint32()));
        f.push_back(arrow::field("total_page_outs",   arrow::uint32()));
        f.push_back(arrow::field("avg_avail_frames",  arrow::uint32()));
        f.push_back(arrow::field("max_avail_frames",  arrow::uint32()));
        f.push_back(arrow::field("min_avail_frames",  arrow::uint32()));
        f.push_back(arrow::field("fixed_frames",      arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf71Record& r, std::string_view src, int64_t ingest_us) {
        common.append_n(r.header, r.subtype, src, ingest_us, 1);
        append_ts(interval_start_ts.get(),
                  datetime_to_epoch_us(r.product.interval_start_date, r.product.interval_start_time));
        arrow_ok(interval_ms      ->Append(r.product.interval_hund * 10u));
        arrow_ok(sysplex_name     ->Append(r.product.sysplex_name));
        arrow_ok(total_page_ins   ->Append(r.paging.total_page_ins));
        arrow_ok(total_page_outs  ->Append(r.paging.total_page_outs));
        arrow_ok(avg_avail_frames ->Append(r.paging.avg_avail_frames));
        arrow_ok(max_avail_frames ->Append(r.paging.max_avail_frames));
        arrow_ok(min_avail_frames ->Append(r.paging.min_avail_frames));
        arrow_ok(fixed_frames     ->Append(r.paging.fixed_frames));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(total_page_ins.get()));
        c.push_back(fin(total_page_outs.get()));   c.push_back(fin(avg_avail_frames.get()));
        c.push_back(fin(max_avail_frames.get()));  c.push_back(fin(min_avail_frames.get()));
        c.push_back(fin(fixed_frames.get()));
        return c;
    }
};

class Smf71_1ParquetSink {
public:
    Smf71_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf71-1", std::move(customer), Smf71_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf71Record& r) {
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
    PartitionedTable<Smf71_1Builders> table_;
};

} // namespace s2p
