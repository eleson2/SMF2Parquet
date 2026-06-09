#pragma once
/*
 * sinks/smf70_2_parquet_sink.h — Parquet sink for SMF type 70 s2 (Crypto Activity).
 * Multi-row: one output row per crypto card.
 */

#include "smf/smf70_2_reader.h"   // smf::Smf70_2Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf70_2Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt16Builder>    card_index;
    std::shared_ptr<arrow::UInt16Builder>    card_type;
    std::shared_ptr<arrow::DoubleBuilder>    ops_count;
    std::shared_ptr<arrow::DoubleBuilder>    exec_time;

    explicit Smf70_2Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          card_index  (std::make_shared<arrow::UInt16Builder>(pool)),
          card_type   (std::make_shared<arrow::UInt16Builder>(pool)),
          ops_count   (std::make_shared<arrow::DoubleBuilder>(pool)),
          exec_time   (std::make_shared<arrow::DoubleBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("card_index",     arrow::uint16()));
        f.push_back(arrow::field("card_type",      arrow::uint16()));
        f.push_back(arrow::field("ops_count",      arrow::float64()));
        f.push_back(arrow::field("exec_time",      arrow::float64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf70_2Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.cards.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const uint32_t int_ms = r.product.interval_hund * 10u;
        const auto& sysplex = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& c : r.cards) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms ->Append(int_ms));
            arrow_ok(sysplex_name->Append(sysplex));
            arrow_ok(card_index  ->Append(c.card_index));
            arrow_ok(card_type   ->Append(c.card_type));
            arrow_ok(ops_count   ->Append(c.ops_count));
            arrow_ok(exec_time   ->Append(c.exec_time));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(card_index.get()));
        c.push_back(fin(card_type.get()));         c.push_back(fin(ops_count.get()));
        c.push_back(fin(exec_time.get()));
        return c;
    }
};

class Smf70_2ParquetSink {
public:
    Smf70_2ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf70-2", std::move(customer), Smf70_2Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf70_2Record& r) {
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
    PartitionedTable<Smf70_2Builders> table_;
};

} // namespace s2p
