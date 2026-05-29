#pragma once
/*
 * smf/smf77_reader.h — SMF Type 77 record parser (parse core): RMF coupling facility activity.
 *
 * Type 77 records describe Parallel Sysplex coupling facility (CF) activity:
 * structure names, request rates (read/write/invalidate), and CF link
 * utilisation per measurement interval.
 *
 * Subtypes:
 *   1  Coupling facility activity
 *   2  CF structure activity
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf77Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    // TODO: CF name, structure name, read/write/invalidate request rates, link utilisation
};

[[nodiscard]] inline Smf77Record read_smf77(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf77Record out;
    out.header  = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
