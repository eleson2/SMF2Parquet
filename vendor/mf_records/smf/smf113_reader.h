#pragma once
/*
 * smf/smf113_reader.h — SMF Type 113 record parser (parse core): CPU MF Counters.
 *
 * Type 113 records provide hardware counters (CPI, cache misses).
 *
 * Record structure (z/OS 3.1, IBM SA23-2260 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, 1=Delta, 2=Absolute)
 *   Offset 22+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, 4-2-2 format for 113 as per search)
 *
 * Triplets (IBM SA23-1370):
 *   [0] Subsystem Section    (off 26, 4-2-2)
 *   [1] Identification       (off 34, 4-2-2)
 *   [2] Counter Set Data     (off 42, 4-2-2)
 *
 * Counter Set Data Section:
 *   Contains physical CPU ID and the counter values.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf113/SMF113S1/
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
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    // Data Section
    CPU_ID        = 100,
    CPU_CLASS     = 101,
    COUNTER_0     = 200, // Cycle Count
    COUNTER_1     = 201, // Instruction Count
    COUNTER_2     = 202, // L1 I-Cache Directory Write
    COUNTER_3     = 203, // L1 I-Cache Penalty
};
} // namespace smf113

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf113Id {
    uint16_t cpu_id{0};     // SMF113_1_CPUID / physical CPU addr
    uint8_t  cpu_class{0};  // SMF113_1_CpuProcClass (0=CP, 4=zIIP)
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

/* ── Section parsers ────────────────────────────────────────────────────── */

// SMF 113 also uses 4-2-2 triplets
[[nodiscard]] inline SectionPtr read_triplet_422(mf::Reader& r) {
    SectionPtr p;
    if (!r.can_read(8)) return p;
    p.offset = r.read_u32();
    p.length = r.read_u16();
    p.count  = r.read_u16();
    return p;
}

/* ── Main SMF113 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf113Record read_smf113(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf113Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    // Triplet area starts at offset 26 for type 113 (as per SA23-1370)
    r.pos = 26;
    const SectionPtr subs_ptr = read_triplet_422(r); // [0] Subsystem
    const SectionPtr iden_ptr = read_triplet_422(r); // [1] Identification
    const SectionPtr data_ptr = read_triplet_422(r); // [2] Data

    mf::Reader sr{rec_bytes};

    // Identification Section contains CPU info
    if (iden_ptr.count > 0 && iden_ptr.length >= 32) {
        sr.pos = iden_ptr.offset;
        sr.skip(14); // skip to SMF113_x_CPUID
        if (sr.can_read(2)) out.id.cpu_id = sr.read_u16();
        sr.skip(12); // skip to SMF113_x_CpuProcClass
        if (sr.can_read(1)) out.id.cpu_class = sr.read_u8();
    }

    // Data Section contains the repeating 8-byte counters
    if (data_ptr.count > 0 && data_ptr.length >= 8) {
        out.counters.reserve(data_ptr.count);
        sr.pos = data_ptr.offset;
        for (uint32_t i = 0; i < data_ptr.count; ++i) {
            if (sr.can_read(8)) out.counters.push_back(sr.read_u64());
            else break;
        }
    }

    return out;
}

} // namespace smf
