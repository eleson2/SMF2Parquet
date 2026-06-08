#pragma once
/*
 * sinks/smf74_2_member_parquet_sink.h — Parquet sink for SMF type 74 s2 (XCF Member).
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

struct Smf74_2_MemberBuilders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::StringBuilder>    system_name;
    std::shared_ptr<arrow::StringBuilder>    group_name;
    std::shared_ptr<arrow::StringBuilder>    member_name;
    std::shared_ptr<arrow::UInt32Builder>    signals_sent;
    std::shared_ptr<arrow::UInt32Builder>    signals_rcvd;

    explicit Smf74_2_MemberBuilders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          system_name (std::make_shared<arrow::StringBuilder>(pool)),
          group_name  (std::make_shared<arrow::StringBuilder>(pool)),
          member_name (std::make_shared<arrow::StringBuilder>(pool)),
          signals_sent(std::make_shared<arrow::UInt32Builder>(pool)),
          signals_rcvd(std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("system_name",    arrow::utf8()));
        f.push_back(arrow::field("group_name",     arrow::utf8()));
        f.push_back(arrow::field("member_name",    arrow::utf8()));
        f.push_back(arrow::field("signals_sent",   arrow::uint32()));
        f.push_back(arrow::field("signals_rcvd",   arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_2Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        for (const auto& m : r.members) {
            common.append(r.header, r.subtype, src, ingest_us);
            append_ts(interval_start_ts.get(), ist);
            arrow_ok(sysplex_name->Append(r.product.sysplex_name));
            arrow_ok(system_name ->Append(m.system_name));
            arrow_ok(group_name  ->Append(m.group_name));
            arrow_ok(member_name ->Append(m.member_name));
            arrow_ok(signals_sent->Append(m.signals_sent));
            arrow_ok(signals_rcvd->Append(m.signals_rcvd));
        }
        return r.members.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(system_name.get()));      c.push_back(fin(group_name.get()));
        c.push_back(fin(member_name.get()));      c.push_back(fin(signals_sent.get()));
        c.push_back(fin(signals_rcvd.get()));
        return c;
    }
};

class Smf74_2_MemberParquetSink {
public:
    Smf74_2_MemberParquetSink(std::string customer, std::string out_root, std::string run_id,
                            std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-2-member", std::move(customer), Smf74_2_MemberBuilders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_2Record& r) {
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
    PartitionedTable<Smf74_2_MemberBuilders> table_;
};

} // namespace s2p
