#pragma once
/*
 * sinks/smf113_parquet_sink.h — Parquet sink for SMF type 113 (CPU Counters).
 */

#include "smf/smf113_reader.h"   // smf::Smf113Record
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf113Builders {
    CommonColumns common;
    std::shared_ptr<arrow::UInt16Builder> cpu_id;
    std::shared_ptr<arrow::UInt8Builder>  cpu_class;
    std::shared_ptr<arrow::ListBuilder>   counters;
    std::shared_ptr<arrow::UInt64Builder> counters_item;

    explicit Smf113Builders(arrow::MemoryPool* pool)
        : common(pool),
          cpu_id   (std::make_shared<arrow::UInt16Builder>(pool)),
          cpu_class(std::make_shared<arrow::UInt8Builder>(pool)) {
        counters_item = std::make_shared<arrow::UInt64Builder>(pool);
        counters = std::make_shared<arrow::ListBuilder>(pool, counters_item);
    }

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("cpu_id",    arrow::uint16()));
        f.push_back(arrow::field("cpu_class", arrow::uint8()));
        f.push_back(arrow::field("counters",  arrow::list(arrow::uint64())));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf113Record& r, std::string_view src, int64_t ingest_us) {
        common.append_n(r.header, r.subtype, src, ingest_us, 1);
        arrow_ok(cpu_id   ->Append(r.id.cpu_id));
        arrow_ok(cpu_class->Append(r.id.cpu_class));
        
        arrow_ok(counters->Append());
        for (auto v : r.counters) {
            arrow_ok(counters_item->Append(v));
        }
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(cpu_id.get()));
        c.push_back(fin(cpu_class.get()));
        c.push_back(fin(counters.get()));
        return c;
    }
};

class Smf113ParquetSink {
public:
    Smf113ParquetSink(std::string customer, std::string out_root, std::string run_id,
                    std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf113", std::move(customer), Smf113Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf113Record& r) {
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
    PartitionedTable<Smf113Builders> table_;
};

} // namespace s2p
