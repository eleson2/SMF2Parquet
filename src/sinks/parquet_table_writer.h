#pragma once
/*
 * sinks/parquet_table_writer.h — one Parquet file, written atomically.
 *
 * Owns a single parquet::arrow::FileWriter for one output path. Rows are handed
 * over as Arrow RecordBatches. The file is written to "<path>.tmp" and renamed
 * to the final path on close(), so readers never observe a partial file and a
 * crashed run leaves no visible output (DESIGN §7.3).
 *
 * The per-type Parquet sinks build their RecordBatches and feed this writer.
 * A future PartitionedParquetWriter will own one of these per (type, smf_date)
 * partition; the writer itself stays partition-agnostic.
 *
 * Requires Apache Arrow + Parquet C++ (libarrow-dev, libparquet-dev).
 */

#include "arrow_helpers.h"   // s2p::arrow_ok

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/util/compression.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace s2p {

class ParquetTableWriter {
public:
    ParquetTableWriter(std::string path,
                       std::shared_ptr<arrow::Schema> schema,
                       arrow::Compression::type compression = arrow::Compression::ZSTD)
        : final_path_(std::move(path)), schema_(std::move(schema)) {
        tmp_path_ = final_path_ + ".tmp";

        auto outfile_res = arrow::io::FileOutputStream::Open(tmp_path_);
        if (!outfile_res.ok())
            throw std::runtime_error("open " + tmp_path_ + ": " + outfile_res.status().ToString());
        std::shared_ptr<arrow::io::FileOutputStream> outfile = std::move(outfile_res).ValueUnsafe();

        auto writer_res = parquet::arrow::FileWriter::Open(
            *schema_, arrow::default_memory_pool(), outfile,
            parquet::WriterProperties::Builder().compression(compression)->build(),
            parquet::ArrowWriterProperties::Builder().store_schema()->build());
        if (!writer_res.ok())
            throw std::runtime_error("parquet writer for " + tmp_path_ + ": " +
                                     writer_res.status().ToString());
        writer_ = std::move(writer_res).ValueUnsafe();
    }

    void write_batch(const std::shared_ptr<arrow::RecordBatch>& batch) {
        arrow_ok(writer_->WriteRecordBatch(*batch));
    }

    // Flush, close the file, and atomically rename tmp → final.
    void close() {
        if (closed_) return;
        closed_ = true;
        arrow_ok(writer_->Close());
        writer_.reset();
        std::error_code ec;
        std::filesystem::rename(tmp_path_, final_path_, ec);
        if (ec)
            throw std::runtime_error("rename " + tmp_path_ + " -> " + final_path_ + ": " + ec.message());
    }

    ~ParquetTableWriter() {
        if (!closed_ && writer_) { try { (void)writer_->Close(); } catch (...) {} }
    }

    ParquetTableWriter(const ParquetTableWriter&)            = delete;
    ParquetTableWriter& operator=(const ParquetTableWriter&) = delete;

private:
    std::string final_path_;
    std::string tmp_path_;
    std::shared_ptr<arrow::Schema>              schema_;
    std::unique_ptr<parquet::arrow::FileWriter> writer_;
    bool closed_{false};
};

} // namespace s2p
