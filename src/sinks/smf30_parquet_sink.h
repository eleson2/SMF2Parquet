#pragma once
/*
 * sinks/smf30_parquet_sink.h — Parquet sink for SMF type 30 (job/step activity).
 * Reference template: CommonColumns + type fields, routed through PartitionedTable.
 */

#include "smf/smf30_reader.h"   // smf::Smf30Record (via mf_records)
#include "common_columns.h"
#include "partitioned_table.h"

#include <arrow/api.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace s2p {

struct Smf30Builders {
    CommonColumns common;
    std::shared_ptr<arrow::StringBuilder> job_name, step_name, job_id, program_name;
    std::shared_ptr<arrow::DoubleBuilder> elapsed_sec, cpu_sec, tcb_sec, srb_sec, ziip_sec;
    std::shared_ptr<arrow::UInt32Builder> excp_count;

    explicit Smf30Builders(arrow::MemoryPool* pool)
        : common(pool),
          job_name    (std::make_shared<arrow::StringBuilder>(pool)),
          step_name   (std::make_shared<arrow::StringBuilder>(pool)),
          job_id      (std::make_shared<arrow::StringBuilder>(pool)),
          program_name(std::make_shared<arrow::StringBuilder>(pool)),
          elapsed_sec (std::make_shared<arrow::DoubleBuilder>(pool)),
          cpu_sec     (std::make_shared<arrow::DoubleBuilder>(pool)),
          tcb_sec     (std::make_shared<arrow::DoubleBuilder>(pool)),
          srb_sec     (std::make_shared<arrow::DoubleBuilder>(pool)),
          ziip_sec    (std::make_shared<arrow::DoubleBuilder>(pool)),
          excp_count  (std::make_shared<arrow::UInt32Builder>(pool)) {}

    static std::shared_ptr<arrow::Schema> schema() {
        arrow::FieldVector f;
        CommonColumns::add_fields(f);
        f.push_back(arrow::field("job_name",     arrow::utf8()));
        f.push_back(arrow::field("step_name",    arrow::utf8()));
        f.push_back(arrow::field("job_id",       arrow::utf8()));
        f.push_back(arrow::field("program_name", arrow::utf8()));
        f.push_back(arrow::field("elapsed_sec",  arrow::float64()));
        f.push_back(arrow::field("cpu_sec",      arrow::float64()));
        f.push_back(arrow::field("tcb_sec",      arrow::float64()));
        f.push_back(arrow::field("srb_sec",      arrow::float64()));
        f.push_back(arrow::field("ziip_sec",     arrow::float64()));
        f.push_back(arrow::field("excp_count",   arrow::uint32()));
        return arrow::schema(f);
    }

    uint64_t append(const smf::Smf30Record& r, std::string_view src, int64_t ingest_us) {
        common.append(r.header, r.subtype, src, ingest_us);
        arrow_ok(job_name    ->Append(r.id.job_name));
        arrow_ok(step_name   ->Append(r.id.step_name));
        arrow_ok(job_id      ->Append(r.id.job_id));
        arrow_ok(program_name->Append(r.id.program_name));
        arrow_ok(elapsed_sec ->Append(r.perf.elapsed_time_hund / 100.0));
        arrow_ok(cpu_sec     ->Append(r.perf.cpu_time_hund     / 100.0));
        arrow_ok(tcb_sec     ->Append(r.proc.tcb_time_hund     / 100.0));
        arrow_ok(srb_sec     ->Append(r.proc.srb_time_hund     / 100.0));
        arrow_ok(ziip_sec    ->Append(r.proc.ziip_time_hund    / 100.0));
        arrow_ok(excp_count  ->Append(r.io.excp_count));
        return 1;
    }

    arrow::ArrayVector finish() {
        arrow::ArrayVector c;
        common.finish_into(c);
        c.push_back(fin(job_name.get()));    c.push_back(fin(step_name.get()));
        c.push_back(fin(job_id.get()));      c.push_back(fin(program_name.get()));
        c.push_back(fin(elapsed_sec.get())); c.push_back(fin(cpu_sec.get()));
        c.push_back(fin(tcb_sec.get()));     c.push_back(fin(srb_sec.get()));
        c.push_back(fin(ziip_sec.get()));    c.push_back(fin(excp_count.get()));
        return c;
    }
};

class Smf30ParquetSink {
public:
    Smf30ParquetSink(std::string out_root, std::string run_id,
                     std::string source_file, int64_t ingest_us)
        : source_(std::move(source_file)), ingest_(ingest_us),
          table_("smf30", Smf30Builders::schema(), std::move(out_root), std::move(run_id)) {}

    void write(const smf::Smf30Record& r) {
        const auto day = CommonColumns::partition_day(r.header);
        const uint64_t n = table_.partition(day).append(r, source_, ingest_);
        table_.added(day, n);
    }
    void close() { table_.close(); }
    uint64_t rows() const noexcept { return table_.rows(); }

private:
    std::string source_;
    int64_t     ingest_;
    PartitionedTable<Smf30Builders> table_;
};

} // namespace s2p
