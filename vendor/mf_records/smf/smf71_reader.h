#pragma once
/*
 * smf/smf71_reader.h — SMF Type 71 record parser (parse core): RMF I/O activity.
 *
 * Type 71 records describe channel subsystem activity: channel path utilisation,
 * I/O rates, error counts, and queue depths for each channel path.
 *
 * No subtype — one record per measurement interval per channel path group.
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf71Record {
    mf::SmfHeader header;
    // No subtype for type 71
    // TODO: channel path ID, utilisation pct, I/O rates, error counts
};

[[nodiscard]] inline Smf71Record read_smf71(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf71Record out;
    out.header = mf::read_smf_header(r);
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
