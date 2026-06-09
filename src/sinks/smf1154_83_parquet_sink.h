#pragma once
/*
 * sinks/smf1154_83_parquet_sink.h — Parquet sink for SMF type 1154 subtype 83 (RACF).
 */

#include "smf/smf1154_reader.h"
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf1154_83Builders {
    CommonColumns common;
    std::shared_ptr<arrow::StringBuilder> system_name;
    std::shared_ptr<arrow::StringBuilder> sysplex_name;
    std::shared_ptr<arrow::StringBuilder> job_name;
    std::shared_ptr<arrow::StringBuilder> request_id;
    std::shared_ptr<arrow::BooleanBuilder> racf_active;
    std::shared_ptr<arrow::UInt16Builder> min_password_len;
    std::shared_ptr<arrow::UInt16Builder> password_history;
    std::shared_ptr<arrow::UInt16Builder> password_interval;

    explicit Smf1154_83Builders(arrow::MemoryPool* pool)
        : common(pool),
          system_name (std::make_shared<arrow::StringBuilder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          job_name    (std::make_shared<arrow::StringBuilder>(pool)),
          request_id  (std::make_shared<arrow::StringBuilder>(pool)),
          racf_active (std::make_shared<arrow::BooleanBuilder>(pool)),
          min_password_len(std::make_shared<arrow::UInt16Builder>(pool)),
          password_history(std::make_shared<arrow::UInt16Builder>(pool)),
          password_interval(std::make_shared<arrow::UInt16Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("system_name",  arrow::utf8()));
        f.push_back(arrow::field("sysplex_name", arrow::utf8()));
        f.push_back(arrow::field("job_name",     arrow::utf8()));
        f.push_back(arrow::field("request_id",   arrow::utf8()));
        f.push_back(arrow::field("racf_active",  arrow::boolean()));
        f.push_back(arrow::field("min_password_len",  arrow::uint16()));
        f.push_back(arrow::field("password_history",  arrow::uint16()));
        f.push_back(arrow::field("password_interval", arrow::uint16()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf1154Record& r, std::string_view src, int64_t ingest_us) {
        if (r.subtype != 83) return 0;
        const uint64_t n = r.racf_summaries.size();
        if (n == 0) return 0;

        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& s : r.racf_summaries) {
            arrow_ok(system_name ->Append(r.common.system_name));
            arrow_ok(sysplex_name->Append(r.common.sysplex_name));
            arrow_ok(job_name    ->Append(r.common.job_name));
            arrow_ok(request_id  ->Append(r.common.request_id));
            arrow_ok(racf_active ->Append(s.is_active));
            arrow_ok(min_password_len->Append(s.min_password_len));
            arrow_ok(password_history->Append(s.password_history));
            arrow_ok(password_interval->Append(s.password_interval));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(system_name.get()));       c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(job_name.get()));          c.push_back(fin(request_id.get()));
        c.push_back(fin(racf_active.get()));       c.push_back(fin(min_password_len.get()));
        c.push_back(fin(password_history.get()));  c.push_back(fin(password_interval.get()));
        return c;
    }
};

class Smf1154_83ParquetSink {
public:
    Smf1154_83ParquetSink(std::string customer, std::string out_root, std::string run_id,
                        std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf1154-83", std::move(customer), Smf1154_83Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf1154Record& r) {
        if (r.subtype != 83) return;
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
    PartitionedTable<Smf1154_83Builders> table_;
};

} // namespace s2p
