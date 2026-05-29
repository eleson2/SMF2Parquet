#pragma once
/*
 * sinks/smf99_parquet_sink.h — Parquet sink for SMF type 99 (user-defined).
 * Layout is site-specific; for now we record the common columns + body length.
 */

#include "smf/smf99_reader.h"   // smf::Smf99Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf99Builders {
    CommonColumns common;
    std::shared_ptr<arrow::UInt32Builder> body_length;

    explicit Smf99Builders(arrow::MemoryPool* pool)
        : common(pool), body_length(std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("body_length", arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf99Record& r, std::string_view src, int64_t ingest_us) {
        common.append(r.header, r.subtype, src, ingest_us);
        arrow_ok(body_length->Append(r.body_length));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(body_length.get()));
        return c;
    }
};

class Smf99ParquetSink {
public:
    Smf99ParquetSink(std::string customer, std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf99", std::move(customer), Smf99Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf99Record& r) {
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
    PartitionedTable<Smf99Builders> table_;
};

} // namespace s2p
