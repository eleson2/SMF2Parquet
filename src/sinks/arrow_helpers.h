#pragma once
/*
 * sinks/arrow_helpers.h — small shared helpers for the Parquet sink layer.
 */

#include <arrow/api.h>

#include <memory>
#include <optional>
#include <stdexcept>

namespace s2p {

// Throw on a non-OK Arrow status (sinks run in a try/catch in main).
inline void arrow_ok(const arrow::Status& s) {
    if (!s.ok()) throw std::runtime_error(s.ToString());
}

// The temporal type used for every event/lineage timestamp column.
inline std::shared_ptr<arrow::DataType> ts_type() {
    return arrow::timestamp(arrow::TimeUnit::MICRO, "UTC");
}

inline void append_ts(arrow::TimestampBuilder* b, std::optional<int64_t> v) {
    if (v) arrow_ok(b->Append(*v)); else arrow_ok(b->AppendNull());
}

inline void append_date(arrow::Date32Builder* b, std::optional<int32_t> v) {
    if (v) arrow_ok(b->Append(*v)); else arrow_ok(b->AppendNull());
}

// Finish a builder into an Array (resets the builder for the next batch).
inline std::shared_ptr<arrow::Array> fin(arrow::ArrayBuilder* b) {
    std::shared_ptr<arrow::Array> a;
    arrow_ok(b->Finish(&a));
    return a;
}

} // namespace s2p
