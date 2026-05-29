#pragma once
/*
 * sinks/header_only_sink.h — generic per-type sink that writes only the common
 * columns. Used for SMF types whose field-level parsers are still stubs
 * (71, 72, 73, 76, 77, 78, 79, 113): the table captures header + subtype now and
 * gains type-specific columns when its parser is fleshed out.
 *
 * Parametrised by the table name so one class serves all stub types.
 */

#include "common_columns.h"
#include "partitioned_table.h"
#include "smf_reader.h"   // mf::SmfHeader (via mf_records)

#include <arrow/api.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct HeaderOnlyBuilders {
    CommonColumns common;
    explicit HeaderOnlyBuilders(arrow::MemoryPool* pool) : common(pool) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        return arrow::schema(f);
    }

    uint64_t append(const mf::SmfHeader& h, uint16_t st, std::string_view src, int64_t ingest_us) {
        common.append(h, st, src, ingest_us);
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        return c;
    }
};

class HeaderOnlySink {
public:
    HeaderOnlySink(std::string table_name, std::string out_root, std::string run_id,
                   std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_(std::move(table_name), HeaderOnlyBuilders::schema(),
                 std::move(out_root), std::move(run_id)) {}

    void write(const mf::SmfHeader& h, uint16_t subtype) {
        const auto day = CommonColumns::partition_day(h);
        table_.partition(day).append(h, subtype, source_, ingest_);
        table_.added(day, 1);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<HeaderOnlyBuilders> table_;
};

} // namespace s2p
