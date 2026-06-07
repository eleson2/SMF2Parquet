#pragma once
/*
 * smf/smf70_reader.h — SMF Type 70 Subtype 1: RMF CPU Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains system-level
 * CPU utilisation indicators: interval timing, CPC model, zAAP/zIIP online
 * counts, and aggregated logical-CP wait/parked times.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 22+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard order)
 *
 * Assumed triplet order at offset 22:
 *   [0] Product section      (SMF70PRS / SMF70PRL)
 *   [1] CPU control section  (SMF70CCS / SMF70CCL)
 *   [2] CPU data section     (SMF70CPS / SMF70CPL / SMF70CPN)
 *   [3] ASID section         (SMF70ASS — not parsed)
 *   [4] PR/SM partition      (SMF70BCS — not parsed)
 *   [5] Logical processor    (SMF70BVS — not parsed)
 *
 * TODO: verify exact pointer-area layout against IBM GA32-0869 for z/OS 3.1.
 *       Lengths/counts may be halfword (2-byte) fields rather than fullword;
 *       offsets are reliable (first 4 bytes of each triplet entry).
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf70/SMF70S1/
 *   IBM GA32-0869  RMF Programmer's Guide
 *   reference_doc/smf70_s1.md
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>

namespace smf {

namespace smf70 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),   // sentinel: field not found
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    // Product Section
    INTERVAL_MS   = 100,
    SAMPLE_COUNT  = 101,
    SYSPLEX_NAME  = 102,

    // Control Section
    CPC_MODEL     = 200,

    // Aggregated Metrics
    CP_COUNT      = 300,
    CP_WAIT_MS    = 301,
    ZIIP_COUNT    = 302,
    ZIIP_WAIT_MS  = 303,
};

[[nodiscard]] inline FieldID smf70_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    if (name == "SMF70INT")      return FieldID::INTERVAL_MS;
    if (name == "SMF70SAM")      return FieldID::SAMPLE_COUNT;
    if (name == "SMF70XNM")      return FieldID::SYSPLEX_NAME;
    
    if (name == "SMF70MOD")      return FieldID::CPC_MODEL;
    
    if (name == "CP_COUNT")      return FieldID::CP_COUNT;
    if (name == "CP_WAIT_MS")    return FieldID::CP_WAIT_MS;
    if (name == "ZIIP_COUNT")    return FieldID::ZIIP_COUNT;
    if (name == "ZIIP_WAIT_MS")  return FieldID::ZIIP_WAIT_MS;

    return FieldID::NONE;
}
} // namespace smf70

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf70Product {
    // RMF Product Section (SMF70PRO) — layout common to all RMF record types.
    // TODO: verify field offsets within section against IBM GA32-0869 z/OS 3.1.
    mf::MfTime  interval_start_time{};  // SMF70IST: interval start time
    mf::MfDate  interval_start_date{};  // SMF70DAT: interval start date
    uint32_t    interval_hund{0};       // SMF70INT: duration (hundredths of seconds)
    uint16_t    sample_count{0};        // SMF70SAM: number of samples
    std::string sysplex_name;           // SMF70XNM: sysplex name (8 chars EBCDIC)
};

struct Smf70Control {
    // CPU Control Section (SMF70CTL).
    // TODO: verify field offsets within section against IBM GA32-0869 z/OS 3.1.
    std::string cpc_model;          // SMF70MOD: CPC processor family (4 bytes EBCDIC)
    uint16_t    zaap_online{0};     // SMF70IFA: zAAPs online at end of interval
    uint16_t    ziip_online{0};     // SMF70SUP: zIIPs online at end of interval
};

struct Smf70Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    Smf70Product  product;
    Smf70Control  control;
    // CPU data section aggregated by processor type (SMF70TYP):
    //   type 0 = General-Purpose CP
    //   type 2 = zIIP
    uint32_t cp_count{0};          // number of CP-type logical processors
    uint64_t cp_wait_hund{0};      // sum SMF70WAT (hundredths) across CPs
    uint64_t cp_parked_hund{0};    // sum SMF70PAT (hundredths) across CPs
    uint32_t ziip_lp_count{0};     // number of zIIP logical processors
    uint64_t ziip_wait_hund{0};
    uint64_t ziip_parked_hund{0};

    [[nodiscard]] mf::FieldValue get_field(smf70::FieldID fid) const {
        using namespace smf70;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            case FieldID::SAMPLE_COUNT:  return mf::FieldValue::from_int64(product.sample_count);
            case FieldID::SYSPLEX_NAME:  return mf::FieldValue::from_string(product.sysplex_name);
            
            case FieldID::CPC_MODEL:     return mf::FieldValue::from_string(control.cpc_model);
            
            case FieldID::CP_COUNT:      return mf::FieldValue::from_int64(cp_count);
            case FieldID::CP_WAIT_MS:    return mf::FieldValue::from_double(cp_wait_hund * 10.0);
            case FieldID::ZIIP_COUNT:    return mf::FieldValue::from_int64(ziip_lp_count);
            case FieldID::ZIIP_WAIT_MS:  return mf::FieldValue::from_double(ziip_wait_hund * 10.0);
            
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf70Product read_smf70_product(mf::Reader& sr) {
    Smf70Product p;
    // RMF Product Section layout — common to all RMF record types.
    // TODO: verify all offsets against IBM GA32-0869 for z/OS 3.1.
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // offset  0: SMF70MFV version (1 byte)
    sr.skip(8);                                // offset  1: SMF70PRD product name (8 bytes EBCDIC)
    p.interval_start_time = sr.read_mf_time(); // offset  9: SMF70IST (4 bytes hundredths)
    p.interval_start_date = sr.read_mf_date(); // offset 13: SMF70DAT (4 bytes packed YYYYDDD)
    p.interval_hund       = sr.read_u32();     // offset 17: SMF70INT (4 bytes hundredths)
    p.sample_count        = sr.read_u16();     // offset 21: SMF70SAM (2 bytes)
    sr.skip(1);                                // offset 23: SMF70FLA flags
    if (sr.can_read(4))  sr.skip(4);          // offset 24: SMF70CYC sampling cycle
    if (sr.can_read(8))  sr.skip(8);          // offset 28: SMF70MVS software level
    if (sr.can_read(4))  sr.skip(4);          // offset 36: IML(1) PRF(1) PTN(1) SRL(1)
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8));  // offset 40: SMF70XNM TODO: verify
    return p;
}

[[nodiscard]] inline Smf70Control read_smf70_control(mf::Reader& sr) {
    Smf70Control c;
    // CPU Control Section (SMF70CTL).
    // Verified offsets for z/OS 3.1: MOD at 0, MDN at 4, MPC at 12, IFA at 22, SUP at 24.
    if (sr.can_read(4))
        c.cpc_model = mf::rtrim(sr.read_ebcdic(4));  // offset 0: SMF70MOD (4 bytes EBCDIC)
    if (sr.can_read(22 + 2)) {
        sr.pos = 22;
        c.zaap_online = sr.read_u16();              // offset 22: SMF70IFA
        c.ziip_online = sr.read_u16();              // offset 24: SMF70SUP
    }
    return c;
}

/* ── Main SMF70 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf70Record read_smf70(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf70Record out;

    out.header  = mf::read_smf_header(r);  // advances to byte 20
    if (r.can_read(2)) out.subtype = r.read_u16();  // bytes 20-21 (absent on header-only records)

    // Section pointer triplets from byte 22 (offset / length / count, 12 bytes each).
    // TODO: verify exact pointer-area layout against IBM GA32-0869 for z/OS 3.1.
    const SectionPtr prod_ptr = read_section_ptr(r);  // [0] Product section
    const SectionPtr ctrl_ptr = read_section_ptr(r);  // [1] CPU control section
    const SectionPtr cpu_ptr  = read_section_ptr(r);  // [2] CPU data section
    // Remaining pointers (ASID, partition, logical processor) not read yet.

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf70_product(sr);

    if (make_section_reader(rec_bytes, ctrl_ptr, 4, sr))
        out.control = read_smf70_control(sr);

    // CPU data section — one entry per logical CP; aggregate wait/parked by type.
    // cpu_ptr.length = bytes per entry, cpu_ptr.count = number of entries.
    if (cpu_ptr.count > 0 && cpu_ptr.length >= 16) {
        const std::size_t entry_base = cpu_ptr.offset;
        const std::size_t entry_len  = cpu_ptr.length;
        for (uint32_t i = 0; i < cpu_ptr.count; ++i) {
            const std::size_t off = entry_base + static_cast<std::size_t>(i) * entry_len;
            if (off + entry_len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, entry_len) };
            // TODO: verify field offsets within CPU data section entry.
            const uint32_t wait_hund   = er.can_read(4) ? er.read_u32() : 0u; // SMF70WAT
            er.skip(6);   // SMF70CID(2) + SMF70CNF(1) + first 3 of SMF70SER(6) TODO
            er.skip(3);   // remaining SMF70SER bytes
            const uint8_t  cpu_type    = er.can_read(1) ? er.read_u8()  : 0u; // SMF70TYP
            // SMF70PAT (parked time) — offset within entry not yet verified
            const uint32_t parked_hund = 0u;  // TODO: read SMF70PAT once offset confirmed
            if (cpu_type == 0) {   // General-Purpose CP
                ++out.cp_count;
                out.cp_wait_hund   += wait_hund;
                out.cp_parked_hund += parked_hund;
            } else if (cpu_type == 2) {  // zIIP
                ++out.ziip_lp_count;
                out.ziip_wait_hund   += wait_hund;
                out.ziip_parked_hund += parked_hund;
            }
        }
    }

    return out;
}

} // namespace smf
