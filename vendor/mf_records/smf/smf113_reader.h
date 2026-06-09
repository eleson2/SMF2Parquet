#pragma once
/*
 * smf/smf113_reader.h — SMF Type 113 record parser (parse core): CPU MF Counters.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf113 {
enum class FieldID : uint32_t {
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    CPU_ID        = 100,
    CPU_CLASS     = 101,
    COUNTER_0     = 200, 
    COUNTER_1     = 201, 
};
} // namespace smf113

struct Smf113Id {
    uint16_t cpu_id{0};     
    uint8_t  cpu_class{0};  
};

struct Smf113Record {
    mf::SmfHeader        header;
    uint16_t             subtype{0};
    Smf113Id             id;
    std::vector<uint64_t> counters;

    [[nodiscard]] mf::FieldValue get_field(smf113::FieldID fid) const {
        using namespace smf113;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::CPU_ID:        return mf::FieldValue::from_int64(id.cpu_id);
            default:                     return mf::FieldValue::null();
        }
    }
};

[[nodiscard]] inline SectionPtr read_triplet_422(mf::Reader& r) {
    SectionPtr p;
    if (!r.can_read(8)) return p;
    p.offset = r.read_u32();
    p.len    = r.read_u16();
    p.count  = r.read_u16();
    return p;
}

[[nodiscard]] inline Smf113Record read_smf113(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf113Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    r.pos = 26;
    const SectionPtr subs_ptr = read_triplet_422(r);
    const SectionPtr iden_ptr = read_triplet_422(r);
    const SectionPtr data_ptr = read_triplet_422(r);

    mf::Reader sr{rec_bytes};

    if (iden_ptr.count > 0 && iden_ptr.len >= 32) {
        sr.pos = iden_ptr.offset;
        sr.skip(14);
        if (sr.can_read(2)) out.id.cpu_id = sr.read_u16();
        sr.skip(12);
        if (sr.can_read(1)) out.id.cpu_class = sr.read_u8();
    }

    if (data_ptr.len >= 8) {
        const uint32_t actual_count = safe_count(data_ptr, rec_bytes.size());
        if (actual_count > 0) {
            out.counters.reserve(actual_count);
            sr.pos = data_ptr.offset;
            for (uint32_t i = 0; i < actual_count; ++i) {
                if (sr.can_read(8)) out.counters.push_back(sr.read_u64());
                else break;
            }
        }
    }

    return out;
}

} // namespace smf
