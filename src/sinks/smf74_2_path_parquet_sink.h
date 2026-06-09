#pragma once
/*
 * sinks/smf74_2_path_parquet_sink.h — Parquet sink for SMF type 74 s2 (XCF Path).
 */

#include "smf/smf74_2_reader.h"
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_2PathBuilders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    system_name;
    std::shared_ptr<arrow::UInt16Builder>    device_num;
    std::shared_ptr<arrow::UInt8Builder>     direction;
    std::shared_ptr<arrow::UInt32Builder>    signals_sent;
    std::shared_ptr<arrow::StringBuilder>    other_system;

    explicit Smf74_2PathBuilders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          system_name (std::make_shared<arrow::StringBuilder>(pool)),
          device_num  (std::make_shared<arrow::UInt16Builder>(pool)),
          direction   (std::make_shared<arrow::UInt8Builder>(pool)),
          signals_sent(std::make_shared<arrow::UInt32Builder>(pool)),
          other_system(std::make_shared<arrow::StringBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("system_name",    arrow::utf8()));
        f.push_back(arrow::field("device_num",     arrow::uint16()));
        f.push_back(arrow::field("direction",      arrow::uint8()));
        f.push_back(arrow::field("signals_sent",   arrow::uint32()));
        f.push_back(arrow::field("other_system",   arrow::utf8()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_2Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.paths.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& p : r.paths) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(sysplex_name->Append(r.product.sysplex_name));
            arrow_ok(system_name ->Append(p.system_name));
            arrow_ok(device_num  ->Append(p.device_num));
            arrow_ok(direction   ->Append(p.direction));
            arrow_ok(signals_sent->Append(p.signals_sent));
            arrow_ok(other_system->Append(p.other_system));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get()));
        c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(system_name.get()));
        c.push_back(fin(device_num.get()));
        c.push_back(fin(direction.get()));
        c.push_back(fin(signals_sent.get()));
        c.push_back(fin(other_system.get()));
        return c;
    }
};

class Smf74_2_PathParquetSink {
public:
    Smf74_2_PathParquetSink(std::string customer, std::string out_root, std::string run_id,
                       std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-2p", std::move(customer), Smf74_2PathBuilders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_2Record& r) {
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
    PartitionedTable<Smf74_2PathBuilders> table_;
};

} // namespace s2p
