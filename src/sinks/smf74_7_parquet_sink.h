#pragma once
/*
 * sinks/smf74_7_parquet_sink.h — Parquet sink for SMF type 74 s7 (FICON Activity).
 */

#include "smf/smf74_7_reader.h"
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_7Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt16Builder>    port_num;
    std::shared_ptr<arrow::UInt16Builder>    port_addr;
    std::shared_ptr<arrow::DoubleBuilder>    words_rcvd;
    std::shared_ptr<arrow::DoubleBuilder>    words_sent;
    std::shared_ptr<arrow::DoubleBuilder>    frames_rcvd;
    std::shared_ptr<arrow::DoubleBuilder>    frames_sent;

    explicit Smf74_7Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          port_num    (std::make_shared<arrow::UInt16Builder>(pool)),
          port_addr   (std::make_shared<arrow::UInt16Builder>(pool)),
          words_rcvd  (std::make_shared<arrow::DoubleBuilder>(pool)),
          words_sent  (std::make_shared<arrow::DoubleBuilder>(pool)),
          frames_rcvd (std::make_shared<arrow::DoubleBuilder>(pool)),
          frames_sent (std::make_shared<arrow::DoubleBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("port_num",       arrow::uint16()));
        f.push_back(arrow::field("port_addr",      arrow::uint16()));
        f.push_back(arrow::field("words_rcvd",     arrow::float64()));
        f.push_back(arrow::field("words_sent",     arrow::float64()));
        f.push_back(arrow::field("frames_rcvd",    arrow::float64()));
        f.push_back(arrow::field("frames_sent",    arrow::float64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_7Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.ports.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const auto& sn = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);
        for (const auto& p : r.ports) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(sysplex_name->Append(sn));
            arrow_ok(port_num   ->Append(p.port_num));
            arrow_ok(port_addr  ->Append(p.port_addr));
            arrow_ok(words_rcvd ->Append(p.words_rcvd));
            arrow_ok(words_sent ->Append(p.words_sent));
            arrow_ok(frames_rcvd->Append(p.frames_rcvd));
            arrow_ok(frames_sent->Append(p.frames_sent));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(port_num.get()));          c.push_back(fin(port_addr.get()));
        c.push_back(fin(words_rcvd.get()));        c.push_back(fin(words_sent.get()));
        c.push_back(fin(frames_rcvd.get()));       c.push_back(fin(frames_sent.get()));
        return c;
    }
};

class Smf74_7ParquetSink {
public:
    Smf74_7ParquetSink(std::string customer, std::string out_root, std::string run_id,
                      std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-7", std::move(customer), Smf74_7Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_7Record& r) {
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
    PartitionedTable<Smf74_7Builders> table_;
};

} // namespace s2p
