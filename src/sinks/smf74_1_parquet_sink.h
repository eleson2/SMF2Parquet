#pragma once
/*
 * sinks/smf74_parquet_sink.h — Parquet sink for SMF type 74 s1 (RMF DASD device
 * activity). Multi-row: one output row per device in the record.
 */

#include "smf/smf74_reader.h"   // smf::Smf74Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::UInt16Builder>    sample_count;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    device_num;
    std::shared_ptr<arrow::StringBuilder>    volume_serial;
    std::shared_ptr<arrow::StringBuilder>    storage_group;
    std::shared_ptr<arrow::UInt8Builder>     device_flags;
    std::shared_ptr<arrow::UInt32Builder>    ssch_count;
    std::shared_ptr<arrow::UInt32Builder>    connect_ms;
    std::shared_ptr<arrow::UInt32Builder>    pending_ms;
    std::shared_ptr<arrow::UInt32Builder>    active_ms;
    std::shared_ptr<arrow::UInt32Builder>    disconnect_ms;
    std::shared_ptr<arrow::UInt32Builder>    queue_depth;

    explicit Smf74_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          sample_count (std::make_shared<arrow::UInt16Builder>(pool)),
          sysplex_name (std::make_shared<arrow::StringBuilder>(pool)),
          device_num   (std::make_shared<arrow::StringBuilder>(pool)),
          volume_serial(std::make_shared<arrow::StringBuilder>(pool)),
          storage_group(std::make_shared<arrow::StringBuilder>(pool)),
          device_flags (std::make_shared<arrow::UInt8Builder>(pool)),
          ssch_count   (std::make_shared<arrow::UInt32Builder>(pool)),
          connect_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          pending_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          active_ms    (std::make_shared<arrow::UInt32Builder>(pool)),
          disconnect_ms(std::make_shared<arrow::UInt32Builder>(pool)),
          queue_depth  (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",   arrow::uint32()));
        f.push_back(arrow::field("sample_count",  arrow::uint16()));
        f.push_back(arrow::field("sysplex_name",  arrow::utf8()));
        f.push_back(arrow::field("device_num",    arrow::utf8()));
        f.push_back(arrow::field("volume_serial", arrow::utf8()));
        f.push_back(arrow::field("storage_group", arrow::utf8()));
        f.push_back(arrow::field("device_flags",  arrow::uint8()));
        f.push_back(arrow::field("ssch_count",    arrow::uint32()));
        f.push_back(arrow::field("connect_ms",    arrow::uint32()));
        f.push_back(arrow::field("pending_ms",    arrow::uint32()));
        f.push_back(arrow::field("active_ms",     arrow::uint32()));
        f.push_back(arrow::field("disconnect_ms", arrow::uint32()));
        f.push_back(arrow::field("queue_depth",   arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& dev : r.devices) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(r.product.interval_hund * 10u));
            arrow_ok(sample_count ->Append(r.product.sample_count));
            arrow_ok(sysplex_name ->Append(r.product.sysplex_name));
            arrow_ok(device_num   ->Append(dev.device_num));
            arrow_ok(volume_serial->Append(dev.volume_serial));
            arrow_ok(storage_group->Append(dev.storage_group));
            arrow_ok(device_flags ->Append(dev.device_flags));
            arrow_ok(ssch_count   ->Append(dev.ssch_count));
            arrow_ok(connect_ms   ->Append(dev.connect_hund    * 10u));
            arrow_ok(pending_ms   ->Append(dev.pending_hund    * 10u));
            arrow_ok(active_ms    ->Append(dev.active_hund     * 10u));
            arrow_ok(disconnect_ms->Append(dev.disconnect_hund * 10u));
            arrow_ok(queue_depth  ->Append(dev.queue_depth));
        }
        return r.devices.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sample_count.get()));      c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(device_num.get()));        c.push_back(fin(volume_serial.get()));
        c.push_back(fin(storage_group.get()));     c.push_back(fin(device_flags.get()));
        c.push_back(fin(ssch_count.get()));        c.push_back(fin(connect_ms.get()));
        c.push_back(fin(pending_ms.get()));        c.push_back(fin(active_ms.get()));
        c.push_back(fin(disconnect_ms.get()));     c.push_back(fin(queue_depth.get()));
        return c;
    }
};

class Smf74_1ParquetSink {
public:
    Smf74_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-1", std::move(customer), Smf74_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74Record& r) {
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
    PartitionedTable<Smf74_1Builders> table_;
};

} // namespace s2p
