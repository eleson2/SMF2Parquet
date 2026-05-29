#pragma once
/*
 * smf/smf76_reader.h — SMF Type 76 record parser (parse core): RMF storage activity.
 *
 * Type 76 records describe real and virtual storage activity: page-in/page-out
 * rates, frame counts, working set sizes, and swap activity.
 *
 * Subtypes:
 *   1  Paging and swapping activity
 *   2  Segment and page table activity
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf76Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    // TODO: page-in rate, page-out rate, real frames available, swap count
};

[[nodiscard]] inline Smf76Record read_smf76(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf76Record out;
    out.header  = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
