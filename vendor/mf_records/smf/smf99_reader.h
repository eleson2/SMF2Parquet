#pragma once
/*
 * smf/smf99_reader.h — SMF Type 99 record parser (parse core): user-defined records.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>

namespace smf {

namespace smf99 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),
};
} // namespace smf99

struct Smf99Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    uint32_t      body_length{0};

    [[nodiscard]] mf::FieldValue get_field(smf99::FieldID fid) const {
        using namespace smf99;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
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
