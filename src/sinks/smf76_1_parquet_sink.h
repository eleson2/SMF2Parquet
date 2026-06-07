#pragma once
/*
 * sinks/smf76_1_parquet_sink.h — Parquet sink for SMF type 76 s1 (Paging Activity).
 * One output row per record.
 */

#include "smf/smf76_reader.h"   // smf::Smf76Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf76_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    page_ins;
    std::shared_ptr<arrow::UInt32Builder>    page_outs;
    std::shared_ptr<arrow::UInt32Builder>    swap_ins;
    std::shared_ptr<arrow::UInt32Builder>    swap_outs;
    std::shared_ptr<arrow::UInt32Builder>    frame_count;
    std::shared_ptr<arrow::UInt32Builder>    working_set;

    explicit Smf76_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          page_ins    (std::make_shared<arrow::UInt32Builder>(pool)),
          page_outs   (std::make_shared<arrow::UInt32Builder>(pool)),
          swap_ins    (std::make_shared<arrow::UInt32Builder>(pool)),
          swap_outs   (std::make_shared<arrow::UInt32Builder>(pool)),
          frame_count (std::make_shared<arrow::UInt32Builder>(pool)),
          working_set (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("page_ins",       arrow::uint32()));
        f.push_back(arrow::field("page_outs",      arrow::uint32()));
        f.push_back(arrow::field("swap_ins",       arrow::uint32()));
        f.push_back(arrow::field("swap_outs",      arrow::uint32()));
        f.push_back(arrow::field("frame_count",    arrow::uint32()));
        f.push_back(arrow::field("working_set",    arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf76Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        common.append(r.header, r.subtype, src, ingest_us);
        append_ts(interval_start_ts.get(), ist);
        arrow_ok(interval_ms ->Append(r.product.interval_hund * 10u));
        arrow_ok(sysplex_name->Append(r.product.sysplex_name));
        arrow_ok(page_ins   ->Append(r.paging.page_ins));
        arrow_ok(page_outs  ->Append(r.paging.page_outs));
        arrow_ok(swap_ins   ->Append(r.paging.swap_ins));
        arrow_ok(swap_outs  ->Append(r.paging.swap_outs));
        arrow_ok(frame_count->Append(r.paging.frame_count));
        arrow_ok(working_set->Append(r.paging.working_set));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(page_ins.get()));
        c.push_back(fin(page_outs.get()));         c.push_back(fin(swap_ins.get()));
        c.push_back(fin(swap_outs.get()));        c.push_back(fin(frame_count.get()));
        c.push_back(fin(working_set.get()));
        return c;
    }
};

class Smf76_1ParquetSink {
public:
    Smf76_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf76-1", std::move(customer), Smf76_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf76Record& r) {
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
    PartitionedTable<Smf76_1Builders> table_;
};

} // namespace s2p
