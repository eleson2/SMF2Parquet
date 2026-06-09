#pragma once
/*
 * sinks/smf73_1_parquet_sink.h — Parquet sink for SMF type 73 s1 (RMF Channel Path Activity).
 * Multi-row: one output row per valid CHPID.
 */

#include "smf/smf73_reader.h"   // smf::Smf73Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf73_1Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    chpid;
    std::shared_ptr<arrow::UInt8Builder>     chpid_type;
    std::shared_ptr<arrow::StringBuilder>    chpid_acronym;
    std::shared_ptr<arrow::UInt32Builder>    busy_count;

    explicit Smf73_1Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          chpid        (std::make_shared<arrow::StringBuilder>(pool)),
          chpid_type   (std::make_shared<arrow::UInt8Builder>(pool)),
          chpid_acronym(std::make_shared<arrow::StringBuilder>(pool)),
          busy_count   (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",   arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("chpid",          arrow::utf8()));
        f.push_back(arrow::field("chpid_type",     arrow::uint8()));
        f.push_back(arrow::field("chpid_acronym",  arrow::utf8()));
        f.push_back(arrow::field("busy_count",     arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf73Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.chpids.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const uint32_t int_ms = r.product.interval_hund * 10u;
        const auto& sysplex = r.product.sysplex_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);

        char hex[3];
        for (const auto& chp : r.chpids) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(int_ms));
            arrow_ok(sysplex_name ->Append(sysplex));
            std::snprintf(hex, sizeof hex, "%02X", chp.chpid);
            arrow_ok(chpid        ->Append(hex));
            arrow_ok(chpid_type   ->Append(chp.chpid_type));
            arrow_ok(chpid_acronym->Append(chp.chpid_acronym));
            arrow_ok(busy_count   ->Append(chp.busy_count));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(chpid.get()));
        c.push_back(fin(chpid_type.get()));        c.push_back(fin(chpid_acronym.get()));
        c.push_back(fin(busy_count.get()));
        return c;
    }
};

class Smf73_1ParquetSink {
public:
    Smf73_1ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf73-1", std::move(customer), Smf73_1Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf73Record& r) {
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
    PartitionedTable<Smf73_1Builders> table_;
};

} // namespace s2p
