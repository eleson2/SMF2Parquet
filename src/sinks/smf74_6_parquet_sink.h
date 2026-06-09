#pragma once
/*
 * sinks/smf74_6_parquet_sink.h — Parquet sink for SMF type 74 s6 (VTS Activity).
 */

#include "smf/smf74_6_reader.h"
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_6Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    max_virtual_mb;
    std::shared_ptr<arrow::UInt32Builder>    inuse_virtual_pages;
    std::shared_ptr<arrow::UInt32Builder>    min_fixed_mb;
    std::shared_ptr<arrow::UInt32Builder>    inuse_fixed_pages;
    std::shared_ptr<arrow::DoubleBuilder>    metadata_hits;
    std::shared_ptr<arrow::DoubleBuilder>    metadata_misses;

    explicit Smf74_6Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          max_virtual_mb(std::make_shared<arrow::UInt32Builder>(pool)),
          inuse_virtual_pages(std::make_shared<arrow::UInt32Builder>(pool)),
          min_fixed_mb(std::make_shared<arrow::UInt32Builder>(pool)),
          inuse_fixed_pages(std::make_shared<arrow::UInt32Builder>(pool)),
          metadata_hits(std::make_shared<arrow::DoubleBuilder>(pool)),
          metadata_misses(std::make_shared<arrow::DoubleBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("max_virtual_mb", arrow::uint32()));
        f.push_back(arrow::field("inuse_virtual_pages", arrow::uint32()));
        f.push_back(arrow::field("min_fixed_mb",   arrow::uint32()));
        f.push_back(arrow::field("inuse_fixed_pages", arrow::uint32()));
        f.push_back(arrow::field("metadata_hits",   arrow::float64()));
        f.push_back(arrow::field("metadata_misses", arrow::float64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_6Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        common.append_n(r.header, r.subtype, src, ingest_us, 1);
        append_ts(interval_start_ts.get(), ist);
        arrow_ok(sysplex_name->Append(r.product.sysplex_name));
        arrow_ok(max_virtual_mb->Append(r.global.max_virtual_mb));
        arrow_ok(inuse_virtual_pages->Append(r.global.inuse_virtual_pages));
        arrow_ok(min_fixed_mb->Append(r.global.min_fixed_mb));
        arrow_ok(inuse_fixed_pages->Append(r.global.inuse_fixed_pages));
        arrow_ok(metadata_hits->Append(r.global.metadata_hits));
        arrow_ok(metadata_misses->Append(r.global.metadata_misses));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(max_virtual_mb.get()));    c.push_back(fin(inuse_virtual_pages.get()));
        c.push_back(fin(min_fixed_mb.get()));      c.push_back(fin(inuse_fixed_pages.get()));
        c.push_back(fin(metadata_hits.get()));     c.push_back(fin(metadata_misses.get()));
        return c;
    }
};

class Smf74_6ParquetSink {
public:
    Smf74_6ParquetSink(std::string customer, std::string out_root, std::string run_id,
                      std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-6", std::move(customer), Smf74_6Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_6Record& r) {
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
    PartitionedTable<Smf74_6Builders> table_;
};

} // namespace s2p
