#pragma once
/*
 * smf/smf78_reader.h — SMF Type 78 record parser (parse core): RMF network activity.
 *
 * Type 78 records describe z/OS Communications Server network activity:
 * interface names, bytes in/out, packet counts, and error rates per
 * measurement interval.
 *
 * Subtypes:
 *   1  OSA / HiperSockets activity
 *   2  TCP/IP activity
 *   3  VTAM activity
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf78Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    // TODO: interface name, bytes in/out, packets in/out, error count
};

[[nodiscard]] inline Smf78Record read_smf78(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf78Record out;
    out.header  = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
