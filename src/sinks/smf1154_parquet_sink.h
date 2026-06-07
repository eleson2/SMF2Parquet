#pragma once
/*
 * sinks/smf1154_parquet_sink.h — Parquet sink for SMF type 1154 (Compliance Evidence).
 * Multi-row: one output row per evidence entry.
 */

#include "smf/smf1154_reader.h"   // smf::Smf1154Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf1154Builders {
    CommonColumns common;
    std::shared_ptr<arrow::StringBuilder> system_name;
    std::shared_ptr<arrow::StringBuilder> sysplex_name;
    std::shared_ptr<arrow::StringBuilder> job_name;
    std::shared_ptr<arrow::StringBuilder> request_id;
    std::shared_ptr<arrow::StringBuilder> stack_name;
    std::shared_ptr<arrow::UInt8Builder>  ip_version;
    std::shared_ptr<arrow::UInt32Builder> up_time_sec;

    explicit Smf1154Builders(arrow::MemoryPool* pool)
        : common(pool),
          system_name (std::make_shared<arrow::StringBuilder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          job_name    (std::make_shared<arrow::StringBuilder>(pool)),
          request_id  (std::make_shared<arrow::StringBuilder>(pool)),
          stack_name  (std::make_shared<arrow::StringBuilder>(pool)),
          ip_version  (std::make_shared<arrow::UInt8Builder>(pool)),
          up_time_sec (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("system_name",  arrow::utf8()));
        f.push_back(arrow::field("sysplex_name", arrow::utf8()));
        f.push_back(arrow::field("job_name",     arrow::utf8()));
        f.push_back(arrow::field("request_id",   arrow::utf8()));
        f.push_back(arrow::field("stack_name",   arrow::utf8()));
        f.push_back(arrow::field("ip_version",   arrow::uint8()));
        f.push_back(arrow::field("up_time_sec",  arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf1154Record& r, std::string_view src, int64_t ingest_us) {
        for (const auto& s : r.tcp_stacks) {
            common.append(r.header, r.subtype, src, ingest_us);
            arrow_ok(system_name ->Append(r.common.system_name));
            arrow_ok(sysplex_name->Append(r.common.sysplex_name));
            arrow_ok(job_name    ->Append(r.common.job_name));
            arrow_ok(request_id  ->Append(r.common.request_id));
            arrow_ok(stack_name  ->Append(s.stack_name));
            arrow_ok(ip_version  ->Append(s.ip_version));
            arrow_ok(up_time_sec ->Append(s.up_time_sec));
        }
        return r.tcp_stacks.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(system_name.get()));  c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(job_name.get()));     c.push_back(fin(request_id.get()));
        c.push_back(fin(stack_name.get()));   c.push_back(fin(ip_version.get()));
        c.push_back(fin(up_time_sec.get()));
        return c;
    }
};

class Smf1154ParquetSink {
public:
    Smf1154ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf1154", std::move(customer), Smf1154Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf1154Record& r) {
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
    PartitionedTable<Smf1154Builders> table_;
};

} // namespace s2p
