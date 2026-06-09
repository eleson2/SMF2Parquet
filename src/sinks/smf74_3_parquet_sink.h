#pragma once
/*
 * sinks/smf74_3_parquet_sink.h — Parquet sink for SMF type 74 s3 (OMVS Activity).
 */

#include "smf/smf74_3_reader.h"
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf74_3Builders {
    CommonColumns common;
    std::shared_ptr<arrow::TimestampBuilder> interval_start_ts;
    std::shared_ptr<arrow::UInt32Builder>    interval_ms;
    std::shared_ptr<arrow::StringBuilder>    sysplex_name;
    std::shared_ptr<arrow::UInt32Builder>    syscall_count;
    std::shared_ptr<arrow::UInt32Builder>    syscall_cpu_ms;
    std::shared_ptr<arrow::UInt32Builder>    max_processes;
    std::shared_ptr<arrow::UInt32Builder>    max_users;
    std::shared_ptr<arrow::UInt32Builder>    current_processes;
    std::shared_ptr<arrow::UInt32Builder>    current_users;

    explicit Smf74_3Builders(arrow::MemoryPool* pool)
        : common(pool),
          interval_start_ts(std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          interval_ms (std::make_shared<arrow::UInt32Builder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          syscall_count(std::make_shared<arrow::UInt32Builder>(pool)),
          syscall_cpu_ms(std::make_shared<arrow::UInt32Builder>(pool)),
          max_processes(std::make_shared<arrow::UInt32Builder>(pool)),
          max_users    (std::make_shared<arrow::UInt32Builder>(pool)),
          current_processes(std::make_shared<arrow::UInt32Builder>(pool)),
          current_users    (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("interval_start_ts", ts_type()));
        f.push_back(arrow::field("interval_ms",    arrow::uint32()));
        f.push_back(arrow::field("sysplex_name",   arrow::utf8()));
        f.push_back(arrow::field("syscall_count",  arrow::uint32()));
        f.push_back(arrow::field("syscall_cpu_ms", arrow::uint32()));
        f.push_back(arrow::field("max_processes",  arrow::uint32()));
        f.push_back(arrow::field("max_users",      arrow::uint32()));
        f.push_back(arrow::field("current_processes", arrow::uint32()));
        f.push_back(arrow::field("current_users",     arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf74_3Record& r, std::string_view src, int64_t ingest_us) {
        const auto ist = datetime_to_epoch_us(r.product.interval_start_date,
                                              r.product.interval_start_time);
        common.append_n(r.header, r.subtype, src, ingest_us, 1);
        append_ts(interval_start_ts.get(), ist);
        arrow_ok(interval_ms ->Append(r.product.interval_hund * 10u));
        arrow_ok(sysplex_name->Append(r.product.sysplex_name));
        arrow_ok(syscall_count->Append(r.omvs.syscall_count));
        arrow_ok(syscall_cpu_ms->Append(r.omvs.syscall_cpu_ms));
        arrow_ok(max_processes->Append(r.omvs.max_processes));
        arrow_ok(max_users->Append(r.omvs.max_users));
        arrow_ok(current_processes->Append(r.omvs.current_processes));
        arrow_ok(current_users->Append(r.omvs.current_users));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(interval_start_ts.get())); c.push_back(fin(interval_ms.get()));
        c.push_back(fin(sysplex_name.get()));      c.push_back(fin(syscall_count.get()));
        c.push_back(fin(syscall_cpu_ms.get()));    c.push_back(fin(max_processes.get()));
        c.push_back(fin(max_users.get()));         c.push_back(fin(current_processes.get()));
        c.push_back(fin(current_users.get()));
        return c;
    }
};

class Smf74_3ParquetSink {
public:
    Smf74_3ParquetSink(std::string customer, std::string out_root, std::string run_id,
                      std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf74-3", std::move(customer), Smf74_3Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf74_3Record& r) {
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
    PartitionedTable<Smf74_3Builders> table_;
};

} // namespace s2p
