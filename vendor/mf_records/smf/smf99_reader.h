#pragma once
/*
 * smf/smf99_reader.h — SMF Type 99 record parser (parse core): user-defined records.
 *
 * Type 99 records are written by installation-defined exits and programs.
 * The layout beyond the standard SMF header is entirely site-specific.
 *
 * Subtypes are installation-defined.
 *
 * TODO: implement site-specific section parsing once the local format
 *       is known.  Consult your installation's SMF exit documentation.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

struct Smf99Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};      // installation-defined
    uint32_t      body_length{0};  // byte count of unparsed body after header+subtype
};

[[nodiscard]] inline Smf99Record read_smf99(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf99Record out;
    out.header      = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();
    out.body_length = static_cast<uint32_t>(rec_bytes.size() - r.pos);
    return out;
}

} // namespace smf
