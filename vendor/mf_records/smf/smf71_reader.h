#pragma once
/*
 * smf/smf71_reader.h — SMF Type 71 record parser (parse core): RMF Paging Activity.
 *
 * Type 71 records describe paging and storage activity.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 22+    Section pointer area: sequential triplets
 *                 (offset/length/count, offset is 4 bytes, len/count are 2 bytes)
 *
 * Assumed triplet order at offset 24 (SA23-1370):
 *   [0] Product section      (SMF71PRS / SMF71PRL / SMF71PRN)
 *   [1] Paging data section  (SMF71PDS / SMF71PDL / SMF71PDN)
 *   [2] Swap placement       (SMF71SWS / SMF71SWL / SMF71SWN)
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf71/SMF71S1/
 *   IBM GA32-0869  RMF Programmer's Guide
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf71 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    // Product Section
    INTERVAL_MS   = 100,
    SAMPLE_COUNT  = 101,

    // Paging Data
    PAGE_IN_RATE  = 200,
    PAGE_OUT_RATE = 201,
    AVG_AVAIL_FRAMES = 202,
};

[[nodiscard]] inline FieldID smf71_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    return FieldID::NONE;
}
} // namespace smf71

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf71Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf71PagingData {
    // Selection of core paging metrics
    uint32_t total_page_ins{0};      // SMF71PIN
    uint32_t total_page_outs{0};     // SMF71POT
    uint32_t avg_avail_frames{0};    // SMF71AFC
    uint32_t max_avail_frames{0};    // SMF71MFC
    uint32_t min_avail_frames{0};    // SMF71LFC
    uint32_t fixed_frames{0};        // SMF71FFC
};

struct Smf71Record {
    mf::SmfHeader   header;
    uint16_t        subtype{0};
    Smf71Product    product;
    Smf71PagingData paging;

    [[nodiscard]] mf::FieldValue get_field(smf71::FieldID fid) const {
        using namespace smf71;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

// SMF 71 uses 4-2-2 triplets instead of 4-4-4
[[nodiscard]] inline SectionPtr read_rmf_triplet_422(mf::Reader& r) {
    SectionPtr p;
    if (!r.can_read(8)) return p;
    p.offset = r.read_u32();
    p.length = r.read_u16();
    p.count  = r.read_u16();
    return p;
}

[[nodiscard]] inline Smf71Product read_smf71_product(mf::Reader& sr) {
    Smf71Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // SMF71MFV
    sr.skip(8);                                // SMF71PRD
    p.interval_start_time = sr.read_mf_time(); // SMF71IST
    p.interval_start_date = sr.read_mf_date(); // SMF71DAT
    p.interval_hund       = sr.read_u32();     // SMF71INT
    p.sample_count        = sr.read_u16();     // SMF71SAM
    sr.skip(1);                                // SMF71FLA
    if (sr.can_read(4)) sr.skip(4);            // SMF71CYC
    if (sr.can_read(8)) sr.skip(8);            // SMF71MVS
    if (sr.can_read(4)) sr.skip(4);            // IML/PRF/PTN/SRL
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF71XNM
    return p;
}

[[nodiscard]] inline Smf71PagingData read_smf71_paging(mf::Reader& sr) {
    Smf71PagingData d;
    // Paging Data Section (SMF71PAG)
    if (sr.can_read(4)) d.total_page_ins = sr.read_u32();   // SMF71PIN
    if (sr.can_read(4)) d.total_page_outs = sr.read_u32();  // SMF71POT
    sr.skip(8); // VIO metrics
    if (sr.can_read(4)) d.avg_avail_frames = sr.read_u32(); // SMF71AFC
    if (sr.can_read(4)) d.max_avail_frames = sr.read_u32(); // SMF71MFC
    if (sr.can_read(4)) d.min_avail_frames = sr.read_u32(); // SMF71LFC
    sr.skip(4); // SMF71PFR
    if (sr.can_read(4)) d.fixed_frames = sr.read_u32();     // SMF71FFC
    return d;
}

/* ── Main SMF71 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf71Record read_smf71(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf71Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    // Triplet area starts at offset 24 for type 71
    r.pos = 24;
    const SectionPtr prod_ptr = read_rmf_triplet_422(r); // [0] Product
    const SectionPtr pag_ptr  = read_rmf_triplet_422(r); // [1] Paging
    const SectionPtr swap_ptr = read_rmf_triplet_422(r); // [2] Swap

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf71_product(sr);

    if (make_section_reader(rec_bytes, pag_ptr, 32, sr))
        out.paging = read_smf71_paging(sr);

    return out;
}

} // namespace smf
