/*
 * tools/pq_dump.cpp — read a Parquet file back and print schema + rows.
 *
 * A verification / debugging aid for SMF2Parquet output (there is no duckdb or
 * pyarrow in this environment). Built only when SMF2PARQUET_WITH_PARQUET=ON.
 *
 * Usage: pq_dump <file.parquet>
 */

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/pretty_print.h>
#include <parquet/arrow/reader.h>

#include <cstdio>
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <file.parquet>\n", argv[0]);
        return 1;
    }
    auto infile_res = arrow::io::ReadableFile::Open(argv[1]);
    if (!infile_res.ok()) {
        std::fprintf(stderr, "open: %s\n", infile_res.status().ToString().c_str());
        return 2;
    }
    std::shared_ptr<arrow::io::ReadableFile> infile = std::move(infile_res).ValueUnsafe();

    auto reader_res = parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
    if (!reader_res.ok()) {
        std::fprintf(stderr, "OpenFile: %s\n", reader_res.status().ToString().c_str());
        return 2;
    }
    std::unique_ptr<parquet::arrow::FileReader> reader = std::move(reader_res).ValueUnsafe();

    std::shared_ptr<arrow::Table> table;
    auto st = reader->ReadTable(&table);
    if (!st.ok()) { std::fprintf(stderr, "ReadTable: %s\n", st.ToString().c_str()); return 2; }

    std::printf("rows=%lld cols=%d\n",
                static_cast<long long>(table->num_rows()), table->num_columns());
    std::printf("schema:\n%s\n", table->schema()->ToString().c_str());

    auto batch_res = table->CombineChunksToBatch();
    if (batch_res.ok()) {
        std::cout << "data:\n";
        (void)arrow::PrettyPrint(*batch_res.ValueUnsafe(), {}, &std::cout);
        std::cout << std::endl;
    }
    return 0;
}
