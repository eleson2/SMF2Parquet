#pragma once
/*
 * sinks/raw_parquet_sink.h — lossless fallback table for record types without a
 * dedicated sink. Stores the common columns + the full raw record bytes
 * (EBCDIC) as a Binary column, so nothing in the input is dropped (DESIGN §5/§10).
 */

#include "common_columns.h"
#include "partitioned_table.h"
#include "smf_reader.h"   // mf::SmfHeader (via mf_records)

#include <arrow/api.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct RawBuilders {
    CommonColumns common;
    std::shared_ptr<arrow::BinaryBuilder> body;

    explicit RawBuilders(arrow::MemoryPool* pool)
        : common(pool), body(std::make_shared<arrow::BinaryBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("body", arrow::binary()));
        return arrow::schema(f);
    }

    uint64_t append(const mf::SmfHeader& h, uint16_t st,
                    std::span<const std::byte> rec, std::string_view src, int64_t ingest_us) {
        common.append_n(h, st, src, ingest_us, 1);
        arrow_ok(body->Append(reinterpret_cast<const uint8_t*>(rec.data()),
                              static_cast<int32_t>(rec.size())));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(body.get()));
        return c;
    }
};

class RawParquetSink {
public:
    RawParquetSink(std::string customer, std::string out_root, std::string run_id,
                   std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("raw", std::move(customer), RawBuilders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const mf::SmfHeader& h, std::span<const std::byte> rec) {
        const auto day = CommonColumns::partition_day(h);
        const auto& sys = h.system_id;
        auto p = table_.get_partition(sys, day);
        p.builders->append(h, /*subtype=*/0, rec, source_, ingest_);
        table_.added(p, 1);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<RawBuilders> table_;
};

} // namespace s2p
