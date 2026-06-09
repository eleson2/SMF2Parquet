#pragma once
/*
 * sinks/smf74_9_parquet_sink.h — Parquet sink for SMF type 74 s9 (PCIE Activity).
 * Multi-row: one output row per PCIE function.
 */

#include "smf/smf74_9_reader.h"   // smf::Smf74_9Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_9Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    pfid;
    std::shared_ptr<arrow::StringBuilder>    job_name;
    std::shared_ptr<arrow::UInt16Builder>    asid;
    std::shared_ptr<arrow::UInt8Builder>     func_type;
    std::shared_ptr<arrow::UInt64Builder>    load_ops;
    std::shared_ptr<arrow::UInt64Builder>    store_ops;

    explicit Smf74_9Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          pfid          (std::make_shared<arrow::UInt32Builder>(pool)),
          job_name      (std::make_shared<arrow::StringBuilder>(pool)),
          asid          (std::make_shared<arrow::UInt16Builder>(pool)),
          func_type     (std::make_shared<arrow::UInt8Builder>(pool)),
          load_ops      (std::make_shared<arrow::UInt64Builder>(pool)),
          store_ops     (std::make_shared<arrow::UInt64Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("pfid",           arrow::uint32()));
        f.push_back(arrow::field("job_name",       arrow::utf8()));
        f.push_back(arrow::field("asid",           arrow::uint16()));
        f.push_back(arrow::field("func_type",      arrow::uint8()));
        f.push_back(arrow::field("load_ops",       arrow::uint64()));
        f.push_back(arrow::field("store_ops",      arrow::uint64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_9Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.functions.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const auto ims = r.product.interval_hund * 10u;
        const auto& sn = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);
        for (const auto& f : r.functions) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(ims));
            arrow_ok(sysplex_name ->Append(sn));
            arrow_ok(pfid         ->Append(f.pfid));
            arrow_ok(job_name     ->Append(f.job_name));
            arrow_ok(asid         ->Append(f.asid));
            arrow_ok(func_type    ->Append(f.func_type));
            arrow_ok(load_ops     ->Append(f.load_ops));
            arrow_ok(store_ops    ->Append(f.store_ops));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(pfid.get()));
        c.push_back(fin(job_name.get()));          c.push_back(fin(asid.get()));
        c.push_back(fin(func_type.get()));         c.push_back(fin(load_ops.get()));
        c.push_back(fin(store_ops.get()));
        return c;
    }
};

class Smf74_9ParquetSink {
public:
    Smf74_9ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-9", std::move(customer), Smf74_9Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_9Record& r) {
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
    PartitionedTable<Smf74_9Builders> table_;
};

} // namespace s2p
