#pragma once
/*
 * sinks/smf1154_2_parquet_sink.h — Parquet sink for SMF type 1154 subtype 2 (FTP).
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

struct Smf1154_2Builders {
    CommonColumns common;
    std::shared_ptr<arrow::StringBuilder> system_name;
    std::shared_ptr<arrow::StringBuilder> sysplex_name;
    std::shared_ptr<arrow::StringBuilder> job_name;
    std::shared_ptr<arrow::StringBuilder> request_id;
    std::shared_ptr<arrow::BooleanBuilder> anonymous_allowed;
    std::shared_ptr<arrow::UInt16Builder> inactivity_timeout;
    std::shared_ptr<arrow::UInt16Builder> port_min;
    std::shared_ptr<arrow::UInt16Builder> port_max;
    std::shared_ptr<arrow::StringBuilder> sec_session_reuse;

    explicit Smf1154_2Builders(arrow::MemoryPool* pool)
        : common(pool),
          system_name (std::make_shared<arrow::StringBuilder>(pool)),
          sysplex_name(std::make_shared<arrow::StringBuilder>(pool)),
          job_name    (std::make_shared<arrow::StringBuilder>(pool)),
          request_id  (std::make_shared<arrow::StringBuilder>(pool)),
          anonymous_allowed(std::make_shared<arrow::BooleanBuilder>(pool)),
          inactivity_timeout(std::make_shared<arrow::UInt16Builder>(pool)),
          port_min(std::make_shared<arrow::UInt16Builder>(pool)),
          port_max(std::make_shared<arrow::UInt16Builder>(pool)),
          sec_session_reuse(std::make_shared<arrow::StringBuilder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("system_name",  arrow::utf8()));
        f.push_back(arrow::field("sysplex_name", arrow::utf8()));
        f.push_back(arrow::field("job_name",     arrow::utf8()));
        f.push_back(arrow::field("request_id",   arrow::utf8()));
        f.push_back(arrow::field("anonymous_allowed", arrow::boolean()));
        f.push_back(arrow::field("inactivity_timeout", arrow::uint16()));
        f.push_back(arrow::field("port_min",     arrow::uint16()));
        f.push_back(arrow::field("port_max",     arrow::uint16()));
        f.push_back(arrow::field("sec_session_reuse", arrow::utf8()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf1154Record& r, std::string_view src, int64_t ingest_us) {
        if (r.subtype != 2) return 0;
        const uint64_t n = r.ftp_configs.size();
        if (n == 0) return 0;

        common.append_n(r.header, r.subtype, src, ingest_us, n);

        for (const auto& c : r.ftp_configs) {
            arrow_ok(system_name ->Append(r.common.system_name));
            arrow_ok(sysplex_name->Append(r.common.sysplex_name));
            arrow_ok(job_name    ->Append(r.common.job_name));
            arrow_ok(request_id  ->Append(r.common.request_id));
            arrow_ok(anonymous_allowed->Append(c.anonymous_allowed));
            arrow_ok(inactivity_timeout->Append(c.inactivity_timeout));
            arrow_ok(port_min->Append(c.port_min));
            arrow_ok(port_max->Append(c.port_max));
            char reuse_str[2] = { c.sec_session_reuse, '\0' };
            arrow_ok(sec_session_reuse->Append(reuse_str));
        }
        return n;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(system_name.get()));       c.push_back(fin(sysplex_name.get()));
        c.push_back(fin(job_name.get()));          c.push_back(fin(request_id.get()));
        c.push_back(fin(anonymous_allowed.get())); c.push_back(fin(inactivity_timeout.get()));
        c.push_back(fin(port_min.get()));          c.push_back(fin(port_max.get()));
        c.push_back(fin(sec_session_reuse.get()));
        return c;
    }
};

class Smf1154_2ParquetSink {
public:
    Smf1154_2ParquetSink(std::string customer, std::string out_root, std::string run_id,
                       std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf1154-2", std::move(customer), Smf1154_2Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf1154Record& r) {
        if (r.subtype != 2) return;
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
    PartitionedTable<Smf1154_2Builders> table_;
};

} // namespace s2p
