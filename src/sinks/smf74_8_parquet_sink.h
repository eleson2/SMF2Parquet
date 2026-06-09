#pragma once
/*
 * sinks/smf74_8_parquet_sink.h — Parquet sink for SMF type 74 s8 (Enterprise Disk).
 * Multi-row: one output row per extent pool.
 */

#include "smf/smf74_8_reader.h"   // smf::Smf74_8Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_8Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    pool_id;
    std::shared_ptr<arrow::UInt32Builder>    pool_type;
    std::shared_ptr<arrow::UInt32Builder>    real_cap_gb;
    std::shared_ptr<arrow::UInt32Builder>    real_extents;
    std::shared_ptr<arrow::UInt32Builder>    real_alloc;

    explicit Smf74_8Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          pool_id     (std::make_shared<arrow::UInt32Builder>(pool)),
          pool_type   (std::make_shared<arrow::UInt32Builder>(pool)),
          real_cap_gb (std::make_shared<arrow::UInt32Builder>(pool)),
          real_extents(std::make_shared<arrow::UInt32Builder>(pool)),
          real_alloc  (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("pool_id",        arrow::uint32()));
        f.push_back(arrow::field("pool_type",      arrow::uint32()));
        f.push_back(arrow::field("real_cap_gb",    arrow::uint32()));
        f.push_back(arrow::field("real_extents",   arrow::uint32()));
        f.push_back(arrow::field("real_alloc",     arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_8Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.pools.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const auto ims = r.product.interval_hund * 10u;
        const auto& sn = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);
        for (const auto& p : r.pools) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms ->Append(ims));
            arrow_ok(sysplex_name->Append(sn));
            arrow_ok(pool_id     ->Append(p.pool_id));
            arrow_ok(pool_type   ->Append(p.pool_type));
            arrow_ok(real_cap_gb ->Append(p.real_cap_gb));
            arrow_ok(real_extents->Append(p.real_extents));
            arrow_ok(real_alloc  ->Append(p.real_alloc));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(pool_id.get()));
        c.push_back(fin(pool_type.get()));         c.push_back(fin(real_cap_gb.get()));
        c.push_back(fin(real_extents.get()));      c.push_back(fin(real_alloc.get()));
        return c;
    }
};

class Smf74_8ParquetSink {
public:
    Smf74_8ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-8", std::move(customer), Smf74_8Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_8Record& r) {
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
    PartitionedTable<Smf74_8Builders> table_;
};

} // namespace s2p
