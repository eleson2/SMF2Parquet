#pragma once
/*
 * sinks/smf75_parquet_sink.h — Parquet sink for SMF type 75 s1 (RMF page data
 * set activity). Multi-row: one output row per page data set in the record.
 */

#include "smf/smf75_reader.h"   // smf::Smf75Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf75Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::UInt16Builder>    sample_count;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    dsn;
    std::shared_ptr<arrow::StringBuilder>    volume_serial;
    std::shared_ptr<arrow::UInt8Builder>     pst_flags;
    std::shared_ptr<arrow::UInt32Builder>    total_slots;
    std::shared_ptr<arrow::UInt32Builder>    max_slots_used;
    std::shared_ptr<arrow::UInt32Builder>    min_slots_used;
    std::shared_ptr<arrow::UInt32Builder>    avg_slots_used;
    std::shared_ptr<arrow::UInt32Builder>    sio_count;
    std::shared_ptr<arrow::UInt32Builder>    pages_transferred;

    explicit Smf75Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms      (std::make_shared<arrow::UInt32Builder>(pool)),
          sample_count     (std::make_shared<arrow::UInt16Builder>(pool)),
          sysplex_name     (std::make_shared<arrow::StringBuilder>(pool)),
          dsn              (std::make_shared<arrow::StringBuilder>(pool)),
          volume_serial    (std::make_shared<arrow::StringBuilder>(pool)),
          pst_flags        (std::make_shared<arrow::UInt8Builder>(pool)),
          total_slots      (std::make_shared<arrow::UInt32Builder>(pool)),
          max_slots_used   (std::make_shared<arrow::UInt32Builder>(pool)),
          min_slots_used   (std::make_shared<arrow::UInt32Builder>(pool)),
          avg_slots_used   (std::make_shared<arrow::UInt32Builder>(pool)),
          sio_count        (std::make_shared<arrow::UInt32Builder>(pool)),
          pages_transferred(std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",       arrow::uint32()));
        f.push_back(arrow::field("sample_count",      arrow::uint16()));
        f.push_back(arrow::field("sysplex_name",      arrow::utf8()));
        f.push_back(arrow::field("dsn",               arrow::utf8()));
        f.push_back(arrow::field("volume_serial",     arrow::utf8()));
        f.push_back(arrow::field("pst_flags",         arrow::uint8()));
        f.push_back(arrow::field("total_slots",       arrow::uint32()));
        f.push_back(arrow::field("max_slots_used",    arrow::uint32()));
        f.push_back(arrow::field("min_slots_used",    arrow::uint32()));
        f.push_back(arrow::field("avg_slots_used",    arrow::uint32()));
        f.push_back(arrow::field("sio_count",         arrow::uint32()));
        f.push_back(arrow::field("pages_transferred", arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf75Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& ps : r.pagesets) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms      ->Append(r.product.interval_hund * 10u));
            arrow_ok(sample_count     ->Append(r.product.sample_count));
            arrow_ok(sysplex_name     ->Append(r.product.sysplex_name));
            arrow_ok(dsn              ->Append(ps.dsn));
            arrow_ok(volume_serial    ->Append(ps.volume_serial));
            arrow_ok(pst_flags        ->Append(ps.pst_flags));
            arrow_ok(total_slots      ->Append(ps.total_slots));
            arrow_ok(max_slots_used   ->Append(ps.max_used));
            arrow_ok(min_slots_used   ->Append(ps.min_used));
            arrow_ok(avg_slots_used   ->Append(ps.avg_used));
            arrow_ok(sio_count        ->Append(ps.sio_count));
            arrow_ok(pages_transferred->Append(ps.pages_xfer));
        }
        return r.pagesets.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sample_count.get()));      c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(dsn.get()));               c.push_back(fin(volume_serial.get()));
        c.push_back(fin(pst_flags.get()));         c.push_back(fin(total_slots.get()));
        c.push_back(fin(max_slots_used.get()));    c.push_back(fin(min_slots_used.get()));
        c.push_back(fin(avg_slots_used.get()));    c.push_back(fin(sio_count.get()));
        c.push_back(fin(pages_transferred.get()));
        return c;
    }
};

class Smf75ParquetSink {
public:
    Smf75ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf75", std::move(customer), Smf75Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf75Record& r) {
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
    PartitionedTable<Smf75Builders> table_;
};

} // namespace s2p
