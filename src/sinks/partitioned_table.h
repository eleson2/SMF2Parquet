#pragma once
/*
 * sinks/partitioned_table.h — route rows to Hive-partitioned Parquet files.
 *
 * One PartitionedTable per SMF type. Rows are bucketed by smf_date (epoch day)
 * so a batch that straddles midnight splits correctly across two partitions
 * (DESIGN §7.2). Layout:
 *
 *   <out_root>/smf_type=<table>/smf_date=YYYY-MM-DD/<table>-<run_id>.parquet
 *
 * Records whose header date is implausible go to smf_date=__null__.
 * Writers are created lazily on the first non-empty flush, so absent types and
 * records that yield zero rows never create empty files.
 *
 * Builders contract (provided per type):
 *   Builders(arrow::MemoryPool*)
 *   static std::shared_ptr<arrow::Schema> schema()   // column order = file order
 *   arrow::ArrayVector finish()                       // finishes+resets builders
 *   // the owning sink appends rows to Builders directly, then calls added(day, n)
 */

#include "arrow_helpers.h"
#include "parquet_table_writer.h"
#include "time_convert.h"

#include <arrow/api.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace s2p {

template<class Builders>
class PartitionedTable {
public:
    PartitionedTable(std::string table_name,
                     std::shared_ptr<arrow::Schema> schema,
                     std::string out_root,
                     std::string run_id,
                     int64_t batch_rows = 65'536)
        : table_(std::move(table_name)), schema_(std::move(schema)),
          out_root_(std::move(out_root)), run_id_(std::move(run_id)),
          batch_rows_(batch_rows) {}

    // Get (creating if needed) the builders for a record's date partition.
    Builders& partition(std::optional<int32_t> day) {
        const int32_t key = day.value_or(kNoDate);
        auto it = parts_.find(key);
        if (it == parts_.end()) {
            Part p;
            p.key      = key;
            p.builders = std::make_unique<Builders>(arrow::default_memory_pool());
            it = parts_.emplace(key, std::move(p)).first;
        }
        return *it->second.builders;
    }

    // Record that n rows were appended to the day partition; flush at threshold.
    void added(std::optional<int32_t> day, uint64_t n) {
        if (n == 0) return;
        Part& p = parts_.at(day.value_or(kNoDate));
        p.pending += n;
        total_    += n;
        if (p.pending >= static_cast<uint64_t>(batch_rows_)) flush(p);
    }

    void close() {
        for (auto& [key, p] : parts_) {
            flush(p);
            if (p.writer) p.writer->close();
        }
    }

    uint64_t    rows()       const noexcept { return total_; }
    std::size_t partitions() const noexcept { return parts_.size(); }

private:
    static constexpr int32_t kNoDate = INT32_MIN;

    struct Part {
        int32_t                             key{0};
        std::unique_ptr<Builders>           builders;
        std::unique_ptr<ParquetTableWriter> writer;
        uint64_t                            pending{0};
    };

    void flush(Part& p) {
        if (p.pending == 0) return;
        if (!p.writer)
            p.writer = std::make_unique<ParquetTableWriter>(make_path(p.key), schema_);
        p.writer->write_batch(arrow::RecordBatch::Make(
            schema_, static_cast<int64_t>(p.pending), p.builders->finish()));
        p.pending = 0;
    }

    std::string make_path(int32_t key) {
        const std::string date_dir =
            (key == kNoDate) ? std::string("__null__") : epoch_days_to_iso(key);
        const std::filesystem::path dir = std::filesystem::path(out_root_)
                                        / ("smf_type=" + table_)
                                        / ("smf_date=" + date_dir);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / (table_ + "-" + run_id_ + ".parquet")).string();
    }

    std::string table_, out_root_, run_id_;
    std::shared_ptr<arrow::Schema> schema_;
    int64_t  batch_rows_;
    uint64_t total_{0};
    std::map<int32_t, Part> parts_;
};

} // namespace s2p
