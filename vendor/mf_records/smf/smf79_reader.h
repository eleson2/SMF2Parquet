#pragma once
/*
 * smf/smf79_reader.h — SMF Type 79 record parser (parse core): RMF system events.
 *
 * Type 79 records are written when significant system events occur during an
 * RMF measurement interval: configuration changes, IEASYSxx overrides,
 * LPAR weight changes, and processor additions/removals.
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

struct Smf79Record {
    mf::SmfHeader header;
    // No subtype for type 79
    // TODO: event type code, event description, affected resource name
};

[[nodiscard]] inline Smf79Record read_smf79(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf79Record out;
    out.header = mf::read_smf_header(r);
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
