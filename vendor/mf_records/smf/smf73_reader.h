#pragma once
/*
 * smf/smf73_reader.h — SMF Type 73 record parser (parse core): RMF tape activity.
 *
 * Type 73 records describe tape device activity: device address, mount counts,
 * data transfer rates, and error counts per measurement interval.
 *
 * No subtype.
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf73Record {
    mf::SmfHeader header;
    // No subtype for type 73
    // TODO: device address, mount count, data rate (MB/s), error count
};

[[nodiscard]] inline Smf73Record read_smf73(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf73Record out;
    out.header = mf::read_smf_header(r);
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
