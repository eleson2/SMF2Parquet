#pragma once
/*
 * smf/smf113_reader.h — SMF Type 113 record parser (parse core): hardware/firmware events.
 *
 * Type 113 records are written by z/OS when hardware or firmware events occur:
 * machine check interruptions, hardware errors, and channel path failures.
 * They supplement the hardware error log (EREP) with z/OS-level context.
 *
 * Subtypes:
 *   1  Channel path incident
 *   2  Machine check
 *   3  Reconfiguration
 *
 * TODO: implement type-specific section parsing against IBM SA22-7642.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf113Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    // TODO: event code, hardware model, CPC serial, affected resource
};

[[nodiscard]] inline Smf113Record read_smf113(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf113Record out;
    out.header  = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    // TODO: read section triplets and type-specific fields
    return out;
}

} // namespace smf
