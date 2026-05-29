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

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
constexpr int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? static_cast<unsigned>(-3) : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

inline bool plausible(const mf::MfDate& d) noexcept {
    return d.year >= 1970 && d.year <= 9999 &&
           d.month >= 1 && d.month <= 12 &&
           d.day   >= 1 && d.day   <= 31;
}

// date32 value (epoch days), or nullopt for an implausible date.
inline std::optional<int32_t> date_to_epoch_days(const mf::MfDate& d) noexcept {
    if (!plausible(d)) return std::nullopt;
    return static_cast<int32_t>(
        days_from_civil(d.year, static_cast<unsigned>(d.month), static_cast<unsigned>(d.day)));
}

// timestamp[us, UTC] value (epoch microseconds), or nullopt for an implausible date.
inline std::optional<int64_t> datetime_to_epoch_us(const mf::MfDate& d, const mf::MfTime& t) noexcept {
    const auto days = date_to_epoch_days(d);
    if (!days) return std::nullopt;
    const int64_t sod_us =
        (static_cast<int64_t>(t.hour) * 3600 + static_cast<int64_t>(t.min) * 60 + t.sec) * 1'000'000LL +
        static_cast<int64_t>(t.hundredths) * 10'000LL;   // 1 hundredth = 10,000 µs
    return static_cast<int64_t>(*days) * 86'400LL * 1'000'000LL + sod_us;
}

// Inverse of days_from_civil: epoch days -> (year, month, day). (Hinnant.)
constexpr void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365u * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5u * doy + 2) / 153;
    d = doy - (153u * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(yoe) + static_cast<int>(era * 400) + (m <= 2);
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
