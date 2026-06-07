#pragma once
/*
 * sinks/smf98_parquet_sink.h — Parquet sink for SMF type 98 (HF Throughput).
 * Multi-row: one output row per address space consumption entry.
 */

#include "smf/smf98_reader.h"   // smf::Smf98Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf98Builders {
    CommonColumns common;
    std::shared_ptr<arrow::UInt16Builder> asid;
    std::shared_ptr<arrow::StringBuilder> job_name;

    explicit Smf98Builders(arrow::MemoryPool* pool)
        : common(pool),
          asid    (std::make_shared<arrow::UInt16Builder>(pool)),
          job_name(std::make_shared<arrow::StringBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("asid",     arrow::uint16()));
        f.push_back(arrow::field("job_name", arrow::utf8()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf98Record& r, std::string_view src, int64_t ingest_us) {
        for (const auto& c : r.as_consumption) {
            common.append(r.header, r.subtype, src, ingest_us);
            arrow_ok(asid    ->Append(c.asid));
            arrow_ok(job_name->Append(c.job_name));
        }
        return r.as_consumption.size();
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(asid.get()));
        c.push_back(fin(job_name.get()));
        return c;
    }
};

class Smf98ParquetSink {
public:
    Smf98ParquetSink(std::string customer, std::string out_root, std::string run_id,
                   std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf98", std::move(customer), Smf98Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf98Record& r) {
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
    PartitionedTable<Smf98Builders> table_;
};

} // namespace s2p
