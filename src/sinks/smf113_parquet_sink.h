#pragma once
/*
 * sinks/smf113_parquet_sink.h — Parquet sink for SMF type 113 (Hardware Counters).
 * Multi-row: one output row per physical CPU in the record (usually 1, but
 * the data section can repeat).
 */

#include "smf/smf113_reader.h"   // smf::Smf113Record (via mf_records)
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
    std::shared_ptr<arrow::UInt64Builder> cycles;
    std::shared_ptr<arrow::UInt64Builder> instructions;
    std::shared_ptr<arrow::UInt64Builder> l1_miss_dir;
    std::shared_ptr<arrow::UInt64Builder> l1_miss_pen;

    explicit Smf113Builders(arrow::MemoryPool* pool)
        : common(pool),
          cpu_id      (std::make_shared<arrow::UInt16Builder>(pool)),
          cpu_class   (std::make_shared<arrow::UInt8Builder>(pool)),
          cycles      (std::make_shared<arrow::UInt64Builder>(pool)),
          instructions(std::make_shared<arrow::UInt64Builder>(pool)),
          l1_miss_dir (std::make_shared<arrow::UInt64Builder>(pool)),
          l1_miss_pen (std::make_shared<arrow::UInt64Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("cpu_id",       arrow::uint16()));
        f.push_back(arrow::field("cpu_class",    arrow::uint8()));
        f.push_back(arrow::field("cycles",       arrow::uint64()));
        f.push_back(arrow::field("instructions", arrow::uint64()));
        f.push_back(arrow::field("l1_miss_dir",  arrow::uint64()));
        f.push_back(arrow::field("l1_miss_pen",  arrow::uint64()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf113Record& r, std::string_view src, int64_t ingest_us) {
        common.append(r.header, r.subtype, src, ingest_us);
        arrow_ok(cpu_id   ->Append(r.id.cpu_id));
        arrow_ok(cpu_class->Append(r.id.cpu_class));
        
        // Counter interpretation depends on CPU generation; for now we just
        // record the first 4 basic counters if they exist.
        if (r.counters.size() >= 1) arrow_ok(cycles->Append(r.counters[0]));
        else arrow_ok(cycles->AppendNull());
        
        if (r.counters.size() >= 2) arrow_ok(instructions->Append(r.counters[1]));
        else arrow_ok(instructions->AppendNull());
        
        if (r.counters.size() >= 3) arrow_ok(l1_miss_dir->Append(r.counters[2]));
        else arrow_ok(l1_miss_dir->AppendNull());
        
        if (r.counters.size() >= 4) arrow_ok(l1_miss_pen->Append(r.counters[3]));
        else arrow_ok(l1_miss_pen->AppendNull());

        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(cpu_id.get()));       c.push_back(fin(cpu_class.get()));
        c.push_back(fin(cycles.get()));       c.push_back(fin(instructions.get()));
        c.push_back(fin(l1_miss_dir.get()));  c.push_back(fin(l1_miss_pen.get()));
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
        const uint64_t n = table_.partition(sys, day).append(r, source_, ingest_);
        table_.added(sys, day, n);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<Smf113Builders> table_;
};

} // namespace s2p
