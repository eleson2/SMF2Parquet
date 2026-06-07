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
                     std::string customer,
                     std::shared_ptr<arrow::Schema> schema,
                     std::string out_root,
                     std::string run_id,
                     int64_t batch_rows = 65'536)
        : table_(std::move(table_name)), customer_(std::move(customer)),
          schema_(std::move(schema)), out_root_(std::move(out_root)),
          run_id_(std::move(run_id)), batch_rows_(batch_rows) {}

    // Get (creating if needed) the builders for a record's system and date partition.
    Builders& partition(const std::string& system_id, std::optional<int32_t> day) {
        const PartitionKey key{system_id, day.value_or(kNoDate)};
        auto it = parts_.find(key);
        if (it == parts_.end()) {
            Part p;
            p.key      = key;
            p.builders = std::make_unique<Builders>(arrow::default_memory_pool());
            it = parts_.emplace(key, std::move(p)).first;
        }
        return *it->second.builders;
    }

    // Record that n rows were appended; flush at threshold.
    void added(const std::string& system_id, std::optional<int32_t> day, uint64_t n) {
        if (n == 0) return;
        const PartitionKey key{system_id, day.value_or(kNoDate)};
        Part& p = parts_.at(key);
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

    struct PartitionKey {
        std::string system_id;
        int32_t     day;
        bool operator<(const PartitionKey& other) const {
            if (system_id != other.system_id) return system_id < other.system_id;
            return day < other.day;
        }
    };

    struct Part {
        PartitionKey                        key;
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

    static std::string sanitize(std::string_view s) {
        if (s.empty()) return "UNKNOWN";
        std::string res;
        res.reserve(s.size());
        for (unsigned char c : s) {
            if (std::isalnum(c)) res.push_back(static_cast<char>(c));
            else res.push_back('_');
        }
        return res;
    }

    std::string make_path(const PartitionKey& key) {
        int y; unsigned m, d;
        std::string date_str;
        std::string year_dir, month_dir;
        
        if (key.day == kNoDate) {
            date_str = "null";
            year_dir = "year=null";
            month_dir = "month=null";
        } else {
            civil_from_days(key.day, y, m, d);
            char buf[16];
            std::snprintf(buf, sizeof buf, "%04d%02u%02u", y, m, d);
            date_str = buf;
            std::snprintf(buf, sizeof buf, "year=%04d", y);
            year_dir = buf;
            std::snprintf(buf, sizeof buf, "month=%02u", m);
            month_dir = buf;
        }

        const std::string sys = sanitize(key.system_id);
        const std::string cust = sanitize(customer_);

        const std::filesystem::path dir = std::filesystem::path(out_root_)
                                        / cust
                                        / sys
                                        / year_dir
                                        / month_dir;
        
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
             throw std::runtime_error("mkdir " + dir.string() + ": " + ec.message());
        }
        
        // Filename: TYPE-YYYYMMDD-RUNID.parquet (using uppercase for type)
        std::string type_upper = table_;
        for (auto& c : type_upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        
        return (dir / (type_upper + "-" + date_str + "-" + run_id_ + ".parquet")).string();
    }

    std::string table_, customer_, out_root_, run_id_;
    std::shared_ptr<arrow::Schema> schema_;
    int64_t  batch_rows_;
    uint64_t total_{0};
    std::map<PartitionKey, Part> parts_;
};

} // namespace s2p
