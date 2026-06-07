#pragma once
/*
 * sinks/smf74_5_parquet_sink.h — Parquet sink for SMF type 74 s5 (Cache Activity).
 * Multi-row: one output row per device.
 */

#include "smf/smf74_5_reader.h"   // smf::Smf74_5Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_5Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    volser;
    std::shared_ptr<arrow::UInt16Builder>    device_num;
    std::shared_ptr<arrow::UInt32Builder>    read_req;
    std::shared_ptr<arrow::UInt32Builder>    read_hit;
    std::shared_ptr<arrow::UInt32Builder>    write_req;
    std::shared_ptr<arrow::UInt32Builder>    write_hit;

    explicit Smf74_5Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          volser        (std::make_shared<arrow::StringBuilder>(pool)),
          device_num    (std::make_shared<arrow::UInt16Builder>(pool)),
          read_req      (std::make_shared<arrow::UInt32Builder>(pool)),
          read_hit      (std::make_shared<arrow::UInt32Builder>(pool)),
          write_req     (std::make_shared<arrow::UInt32Builder>(pool)),
          write_hit     (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("volser",         arrow::utf8()));
        f.push_back(arrow::field("device_num",     arrow::uint16()));
        f.push_back(arrow::field("read_req",       arrow::uint32()));
        f.push_back(arrow::field("read_hit",       arrow::uint32()));
        f.push_back(arrow::field("write_req",      arrow::uint32()));
        f.push_back(arrow::field("write_hit",      arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_5Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& d : r.devices) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(r.product.interval_hund * 10u));
            arrow_ok(sysplex_name ->Append(r.product.sysplex_name));
            arrow_ok(volser       ->Append(d.volser));
            arrow_ok(device_num   ->Append(d.device_num));
            arrow_ok(read_req     ->Append(d.read_req));
            arrow_ok(read_hit     ->Append(d.read_hit));
            arrow_ok(write_req    ->Append(d.write_req));
            arrow_ok(write_hit    ->Append(d.write_hit));
        }
        return r.devices.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(volser.get()));
        c.push_back(fin(device_num.get()));        c.push_back(fin(read_req.get()));
        c.push_back(fin(read_hit.get()));          c.push_back(fin(write_req.get()));
        c.push_back(fin(write_hit.get()));
        return c;
    }
};

class Smf74_5ParquetSink {
public:
    Smf74_5ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-5", std::move(customer), Smf74_5Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_5Record& r) {
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
    PartitionedTable<Smf74_5Builders> table_;
};

} // namespace s2p
