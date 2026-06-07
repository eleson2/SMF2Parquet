#pragma once
/*
 * sinks/smf79_13_parquet_sink.h — Parquet sink for SMF type 79 s13 (System Evaluation).
 * Multi-row: one output row per system event.
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

struct Smf79_13Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt16Builder>    event_type;
    std::shared_ptr<arrow::StringBuilder>    event_data;

    explicit Smf79_13Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          event_type  (std::make_shared<arrow::UInt16Builder>(pool)),
          event_data  (std::make_shared<arrow::StringBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("event_type",     arrow::uint16()));
        f.push_back(arrow::field("event_data",     arrow::utf8()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf79Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& e : r.events) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(sysplex_name->Append(r.product.sysplex_name));
            arrow_ok(event_type  ->Append(e.event_type));
            arrow_ok(event_data  ->Append(e.event_data));
        }
        return r.events.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(event_type.get()));        c.push_back(fin(event_data.get()));
        return c;
    }
};

class Smf79_13ParquetSink {
public:
    Smf79_13ParquetSink(std::string customer, std::string out_root, std::string run_id,
                      std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf79-13", std::move(customer), Smf79_13Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf79Record& r) {
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
    PartitionedTable<Smf79_13Builders> table_;
};

} // namespace s2p
