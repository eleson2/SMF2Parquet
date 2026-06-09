#pragma once
/*
 * sinks/smf72_3_parquet_sink.h — Parquet sink for SMF type 72 s3 (RMF Workload Activity).
 * Multi-row: one output row per service/report class period.
 */

#include "smf/smf72_reader.h"   // smf::Smf72Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf72_3Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    policy_name;
    std::shared_ptr<arrow::StringBuilder>    workload_name;
    std::shared_ptr<arrow::StringBuilder>    class_name;
    std::shared_ptr<arrow::UInt32Builder>    period_num;
    std::shared_ptr<arrow::UInt32Builder>    importance;
    std::shared_ptr<arrow::UInt64Builder>    service_units;
    std::shared_ptr<arrow::UInt64Builder>    cpu_units;
    std::shared_ptr<arrow::UInt64Builder>    ioc_units;
    std::shared_ptr<arrow::UInt64Builder>    mso_units;
    std::shared_ptr<arrow::UInt64Builder>    srb_units;
    std::shared_ptr<arrow::UInt32Builder>    trans_ended;
    std::shared_ptr<arrow::DoubleBuilder>    trans_elapsed_sec;

    explicit Smf72_3Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms  (std::make_shared<arrow::UInt32Builder>(pool)),
          policy_name  (std::make_shared<arrow::StringBuilder>(pool)),
          workload_name(std::make_shared<arrow::StringBuilder>(pool)),
          class_name   (std::make_shared<arrow::StringBuilder>(pool)),
          period_num   (std::make_shared<arrow::UInt32Builder>(pool)),
          importance   (std::make_shared<arrow::UInt32Builder>(pool)),
          service_units(std::make_shared<arrow::UInt64Builder>(pool)),
          cpu_units    (std::make_shared<arrow::UInt64Builder>(pool)),
          ioc_units    (std::make_shared<arrow::UInt64Builder>(pool)),
          mso_units    (std::make_shared<arrow::UInt64Builder>(pool)),
          srb_units    (std::make_shared<arrow::UInt64Builder>(pool)),
          trans_ended  (std::make_shared<arrow::UInt32Builder>(pool)),
          trans_elapsed_sec(std::make_shared<arrow::DoubleBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",   arrow::uint32()));
        f.push_back(arrow::field("policy_name",   arrow::utf8()));
        f.push_back(arrow::field("workload_name", arrow::utf8()));
        f.push_back(arrow::field("class_name",    arrow::utf8()));
        f.push_back(arrow::field("period_num",    arrow::uint32()));
        f.push_back(arrow::field("importance",    arrow::uint32()));
        f.push_back(arrow::field("service_units", arrow::uint64()));
        f.push_back(arrow::field("cpu_units",     arrow::uint64()));
        f.push_back(arrow::field("ioc_units",     arrow::uint64()));
        f.push_back(arrow::field("mso_units",     arrow::uint64()));
        f.push_back(arrow::field("srb_units",     arrow::uint64()));
        f.push_back(arrow::field("trans_ended",   arrow::uint32()));
        f.push_back(arrow::field("trans_elapsed_sec", arrow::float64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf72Record& r, std::string_view src, int64_t ingest_us) {
        const uint64_t n = r.periods.size();
        if (n == 0) return 0;

        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        const uint32_t int_ms = r.product.interval_hund * 10u;
        const auto& policy   = r.control.policy_name;
        const auto& workload = r.control.workload_name;
        const auto& class_nm = r.control.class_name;

        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& p : r.periods) {
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(interval_ms  ->Append(int_ms));
            arrow_ok(policy_name  ->Append(policy));
            arrow_ok(workload_name->Append(workload));
            arrow_ok(class_name   ->Append(class_nm));
            arrow_ok(period_num   ->Append(p.period_num));
            arrow_ok(importance   ->Append(p.importance));
            arrow_ok(service_units->Append(p.service_units));
            arrow_ok(cpu_units    ->Append(p.cpu_units));
            arrow_ok(ioc_units    ->Append(p.ioc_units));
            arrow_ok(mso_units    ->Append(p.mso_units));
            arrow_ok(srb_units    ->Append(p.srb_units));
            arrow_ok(trans_ended  ->Append(p.trans_ended));
            arrow_ok(trans_elapsed_sec->Append(p.trans_elapsed_hund / 100.0));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(policy_name.get()));       c.push_back(fin(workload_name.get()));
        c.push_back(fin(class_name.get()));        c.push_back(fin(period_num.get()));
        c.push_back(fin(importance.get()));        c.push_back(fin(service_units.get()));
        c.push_back(fin(cpu_units.get()));         c.push_back(fin(ioc_units.get()));
        c.push_back(fin(mso_units.get()));         c.push_back(fin(srb_units.get()));
        c.push_back(fin(trans_ended.get()));       c.push_back(fin(trans_elapsed_sec.get()));
        return c;
    }
};

class Smf72_3ParquetSink {
public:
    Smf72_3ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf72-3", std::move(customer), Smf72_3Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf72Record& r) {
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
    PartitionedTable<Smf72_3Builders> table_;
};

} // namespace s2p
