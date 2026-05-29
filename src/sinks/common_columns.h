#pragma once
/*
 * sinks/common_columns.h — the column block every SMF Parquet table shares.
 *
 * Column order (DESIGN §7.6): the common block comes first, type-specific
 * columns follow. Lineage columns (source_file, ingest_ts) are part of the
 * common block so downstream compaction/dedup can rely on them everywhere.
 */

#include "arrow_helpers.h"
#include "time_convert.h"
#include "smf_reader.h"   // mf::SmfHeader (via mf_records)

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace s2p {

struct CommonColumns {
    std::shared_ptr<arrow::TimestampBuilder> smf_ts;
    std::shared_ptr<arrow::Date32Builder>    smf_date;
    std::shared_ptr<arrow::StringBuilder>    system_id;
    std::shared_ptr<arrow::StringBuilder>    subsystem_id;
    std::shared_ptr<arrow::UInt8Builder>     record_type;
    std::shared_ptr<arrow::UInt16Builder>    subtype;
    std::shared_ptr<arrow::UInt8Builder>     flags;
    std::shared_ptr<arrow::StringBuilder>    source_file;
    std::shared_ptr<arrow::TimestampBuilder> ingest_ts;

    explicit CommonColumns(arrow::MemoryPool* pool)
        : smf_ts      (std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)),
          smf_date    (std::make_shared<arrow::Date32Builder>(pool)),
          system_id   (std::make_shared<arrow::StringBuilder>(pool)),
          subsystem_id(std::make_shared<arrow::StringBuilder>(pool)),
          record_type (std::make_shared<arrow::UInt8Builder>(pool)),
          subtype     (std::make_shared<arrow::UInt16Builder>(pool)),
          flags       (std::make_shared<arrow::UInt8Builder>(pool)),
          source_file (std::make_shared<arrow::StringBuilder>(pool)),
          ingest_ts   (std::make_shared<arrow::TimestampBuilder>(ts_type(), pool)) {}

    static void add_fields(arrow::FieldVector& f) {
        f.push_back(arrow::field("smf_timestamp", ts_type()));
        f.push_back(arrow::field("smf_date",      arrow::date32()));
        f.push_back(arrow::field("system_id",     arrow::utf8()));
        f.push_back(arrow::field("subsystem_id",  arrow::utf8()));
        f.push_back(arrow::field("record_type",   arrow::uint8()));
        f.push_back(arrow::field("subtype",       arrow::uint16()));
        f.push_back(arrow::field("flags",         arrow::uint8()));
        f.push_back(arrow::field("source_file",   arrow::utf8()));
        f.push_back(arrow::field("ingest_ts",     ts_type()));
    }

    void append(const mf::SmfHeader& h, uint16_t st, std::string_view src, int64_t ingest_us) {
        append_ts  (smf_ts.get(),   datetime_to_epoch_us(h.date, h.time));
        append_date(smf_date.get(), date_to_epoch_days(h.date));
        arrow_ok(system_id   ->Append(h.system_id));
        arrow_ok(subsystem_id->Append(h.subsystem_id));
        arrow_ok(record_type ->Append(h.record_type));
        arrow_ok(subtype     ->Append(st));
        arrow_ok(flags       ->Append(h.flags));
        arrow_ok(source_file ->Append(src));
        arrow_ok(ingest_ts   ->Append(ingest_us));
    }

    void finish_into(arrow::ArrayVector& out) {
        out.push_back(fin(smf_ts.get()));
        out.push_back(fin(smf_date.get()));
        out.push_back(fin(system_id.get()));
        out.push_back(fin(subsystem_id.get()));
        out.push_back(fin(record_type.get()));
        out.push_back(fin(subtype.get()));
        out.push_back(fin(flags.get()));
        out.push_back(fin(source_file.get()));
        out.push_back(fin(ingest_ts.get()));
    }

    // Partition key (epoch day) for a record; nullopt if the header date is implausible.
    static std::optional<int32_t> partition_day(const mf::SmfHeader& h) {
        return date_to_epoch_days(h.date);
    }
};

} // namespace s2p
