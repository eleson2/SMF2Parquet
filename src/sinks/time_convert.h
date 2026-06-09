#pragma once
/*
 * sinks/time_convert.h — SMF date/time → Arrow temporal encodings.
 *
 * Arrow/Parquet native temporal types (preferred over int-YYYYMMDD for a
 * DuckDB/Iceberg reporting platform):
 *   - date32                  : days since 1970-01-01
 *   - timestamp[us, UTC]      : microseconds since the Unix epoch
 *
 * Invalid/empty SMF dates (e.g. an all-zero header date) map to std::nullopt so
 * the column is written as NULL rather than a nonsense far-past value.
 */

#include "smf_reader.h"   // mf::MfDate, mf::MfTime (via mf_records)

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace s2p {

// date32 value (epoch days), or nullopt for an implausible date.
inline std::optional<int32_t> date_to_epoch_days(const mf::MfDate& d) noexcept {
    if (!mf::is_plausible_date(d)) return std::nullopt;
    return mf::mf_date_to_epoch_days(d);
}

// timestamp[us, UTC] value (epoch microseconds), or nullopt for an implausible date.
inline std::optional<int64_t> datetime_to_epoch_us(const mf::MfDate& d, const mf::MfTime& t) noexcept {
    if (!mf::is_plausible_date(d)) return std::nullopt;
    return mf::mf_datetime_to_unix_us(d, t);
}

// Inverse of days_from_civil: epoch days -> (year, month, day). (Hinnant.)
constexpr void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) noexcept {
    mf::civil_from_days(z, y, m, d);
}

// epoch day -> "YYYY-MM-DD" (used for the smf_date= Hive partition directory).
inline std::string epoch_days_to_iso(int32_t days) {
    int y; unsigned m, d;
    civil_from_days(days, y, m, d);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", y, m, d);
    return buf;
}

} // namespace s2p
