#pragma once
/*
 * sinks/smf74_4_parquet_sink.h — Parquet sink for SMF type 74 s4 (CF Activity).
 * Multi-row: one output row per CF structure.
 */

#include "smf/smf74_4_reader.h"   // smf::Smf74_4Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_4Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    cf_name;
    std::shared_ptr<arrow::UInt16Builder>    cf_level;
    std::shared_ptr<arrow::UInt32Builder>    total_req;
    std::shared_ptr<arrow::StringBuilder>    structure_name;
    std::shared_ptr<arrow::UInt32Builder>    size_4k;

    explicit Smf74_4Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          cf_name       (std::make_shared<arrow::StringBuilder>(pool)),
          cf_level      (std::make_shared<arrow::UInt16Builder>(pool)),
          total_req     (std::make_shared<arrow::UInt32Builder>(pool)),
          structure_name(std::make_shared<arrow::StringBuilder>(pool)),
          size_4k       (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("cf_name",        arrow::utf8()));
        f.push_back(arrow::field("cf_level",       arrow::uint16()));
        f.push_back(arrow::field("total_req",      arrow::uint32()));
        f.push_back(arrow::field("structure_name", arrow::utf8()));
        f.push_back(arrow::field("size_4k",        arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_4Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.structures.size();
        if (n == 0) return 0;
        
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const uint32_t int_ms = r.product.interval_hund * 10u;
        
        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& s : r.structures) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(int_ms));
            arrow_ok(sysplex_name ->Append(r.product.sysplex_name));
            arrow_ok(cf_name      ->Append(r.cf.cf_name));
            arrow_ok(cf_level     ->Append(r.cf.cf_level));
            arrow_ok(total_req    ->Append(r.cf.total_req));
            arrow_ok(structure_name->Append(s.structure_name));
            arrow_ok(size_4k      ->Append(s.size_4k));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(cf_name.get()));
        c.push_back(fin(cf_level.get()));          c.push_back(fin(total_req.get()));
        c.push_back(fin(structure_name.get()));    c.push_back(fin(size_4k.get()));
        return c;
    }
};

class Smf74_4ParquetSink {
public:
    Smf74_4ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-4", std::move(customer), Smf74_4Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_4Record& r) {
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
    PartitionedTable<Smf74_4Builders> table_;
};

} // namespace s2p
