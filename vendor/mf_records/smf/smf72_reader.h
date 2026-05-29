#pragma once
/*
 * smf/smf72_reader.h — SMF Type 72 record parser (parse core): RMF device activity (DASD).
 *
 * Type 72 records describe individual DASD device activity: device address,
 * response time, queue depth, utilisation, and error rates.
 *
 * Subtypes:
 *   1  DASD device activity
 *   2  Cache statistics
 *   3  Extended remote copy statistics
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf72Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    // TODO: device address, response time (ms), queue depth, utilisation pct
};

[[nodiscard]] inline Smf72Record read_smf72(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf72Record out;
    out.header  = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
