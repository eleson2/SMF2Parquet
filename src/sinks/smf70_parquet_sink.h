#pragma once
/*
 * sinks/smf70_parquet_sink.h — Parquet sink for SMF type 70 s1 (RMF CPU activity).
 */

#include "smf/smf70_reader.h"   // smf::Smf70Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf70Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::UInt16Builder>    sample_count;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    cpc_model;
    std::shared_ptr<arrow::UInt16Builder>    zaap_online;
    std::shared_ptr<arrow::UInt16Builder>    ziip_online;
    std::shared_ptr<arrow::UInt32Builder>    cp_count;
    std::shared_ptr<arrow::UInt32Builder>    cp_wait_ms;
    std::shared_ptr<arrow::UInt32Builder>    cp_parked_ms;
    std::shared_ptr<arrow::UInt32Builder>    ziip_lp_count;
    std::shared_ptr<arrow::UInt32Builder>    ziip_wait_ms;
    std::shared_ptr<arrow::UInt32Builder>    ziip_parked_ms;

    explicit Smf70Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms   (std::make_shared<arrow::UInt32Builder>(pool)),
          sample_count  (std::make_shared<arrow::UInt16Builder>(pool)),
          sysplex_name  (std::make_shared<arrow::StringBuilder>(pool)),
          cpc_model     (std::make_shared<arrow::StringBuilder>(pool)),
          zaap_online   (std::make_shared<arrow::UInt16Builder>(pool)),
          ziip_online   (std::make_shared<arrow::UInt16Builder>(pool)),
          cp_count      (std::make_shared<arrow::UInt32Builder>(pool)),
          cp_wait_ms    (std::make_shared<arrow::UInt32Builder>(pool)),
          cp_parked_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          ziip_lp_count (std::make_shared<arrow::UInt32Builder>(pool)),
          ziip_wait_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          ziip_parked_ms(std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sample_count",   arrow::uint16()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("cpc_model",      arrow::utf8()));
        f.push_back(arrow::field("zaap_online",    arrow::uint16()));
        f.push_back(arrow::field("ziip_online",    arrow::uint16()));
        f.push_back(arrow::field("cp_count",       arrow::uint32()));
        f.push_back(arrow::field("cp_wait_ms",     arrow::uint32()));
        f.push_back(arrow::field("cp_parked_ms",   arrow::uint32()));
        f.push_back(arrow::field("ziip_lp_count",  arrow::uint32()));
        f.push_back(arrow::field("ziip_wait_ms",   arrow::uint32()));
        f.push_back(arrow::field("ziip_parked_ms", arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf70Record& r, std::string_view src, int64_t ingest_us) {
        common.append(r.header, r.subtype, src, ingest_us);
        append_ts(interval_start_ts.get(),
                  datetime_to_epoch_us(r.product.interval_start_date, r.product.interval_start_time));
        arrow_ok(interval_ms   ->Append(r.product.interval_hund * 10u));
        arrow_ok(sample_count  ->Append(r.product.sample_count));
        arrow_ok(sysplex_name  ->Append(r.product.sysplex_name));
        arrow_ok(cpc_model     ->Append(r.control.cpc_model));
        arrow_ok(zaap_online   ->Append(r.control.zaap_online));
        arrow_ok(ziip_online   ->Append(r.control.ziip_online));
        arrow_ok(cp_count      ->Append(r.cp_count));
        arrow_ok(cp_wait_ms    ->Append(static_cast<uint32_t>(r.cp_wait_hund   * 10u)));
        arrow_ok(cp_parked_ms  ->Append(static_cast<uint32_t>(r.cp_parked_hund * 10u)));
        arrow_ok(ziip_lp_count ->Append(r.ziip_lp_count));
        arrow_ok(ziip_wait_ms  ->Append(static_cast<uint32_t>(r.ziip_wait_hund   * 10u)));
        arrow_ok(ziip_parked_ms->Append(static_cast<uint32_t>(r.ziip_parked_hund * 10u)));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sample_count.get()));      c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(cpc_model.get()));         c.push_back(fin(zaap_online.get()));
        c.push_back(fin(ziip_online.get()));       c.push_back(fin(cp_count.get()));
        c.push_back(fin(cp_wait_ms.get()));        c.push_back(fin(cp_parked_ms.get()));
        c.push_back(fin(ziip_lp_count.get()));     c.push_back(fin(ziip_wait_ms.get()));
        c.push_back(fin(ziip_parked_ms.get()));
        return c;
    }
};

class Smf70ParquetSink {
public:
    Smf70ParquetSink(std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf70", Smf70Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf70Record& r) {
        const auto day = CommonColumns::partition_day(r.header);
        const uint64_t n = table_.partition(day).append(r, source_, ingest_);
        table_.added(day, n);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<Smf70Builders> table_;
};

} // namespace s2p
