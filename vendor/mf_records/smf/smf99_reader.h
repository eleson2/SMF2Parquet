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

namespace smf99 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),   // sentinel: field not found
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),
};

[[nodiscard]] inline FieldID smf99_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    return FieldID::NONE;
}
} // namespace smf99

struct Smf99Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};      // installation-defined
    uint32_t      body_length{0};  // byte count of unparsed body after header+subtype

    [[nodiscard]] mf::FieldValue get_field(smf99::FieldID fid) const {
        using namespace smf99;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            default:                     return mf::FieldValue::null();
        }
    }
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
