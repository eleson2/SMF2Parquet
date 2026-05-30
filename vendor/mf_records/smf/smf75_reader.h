#pragma once
/*
 * smf/smf75_reader.h — SMF Type 75 Subtype 1: RMF Page Data Set Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains one page data
 * set section entry (SMF75PSD) per active page data set. Each page dataset
 * becomes one output row — this is a multi-entry record type.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 22-25  SMF75TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard order)
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF75PRS/PRL/PRN)
 *   [1] Page data section    (SMF75PSS/PSL/PSN) — one entry per page dataset
 *
 * TODO: verify exact pointer-area layout and section field byte offsets against
 *       IBM GA32-0869 for z/OS 3.1. The IBM SMF Explorer does not list byte
 *       offsets; sequential field order is assumed from the reference doc.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf75/SMF75S1/
 *   IBM GA32-0869  RMF Programmer's Guide
 *   reference_doc/smf75_s1.md
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf75 {
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

    // Page Data Set Section
    DSN           = 200,
    VOLSER        = 201,
    PST_FLAGS     = 202,
    TOTAL_SLOTS   = 203,
    MAX_USED      = 204,
    MIN_USED      = 205,
    AVG_USED      = 206,
    SIO_COUNT     = 207,
    PAGES_XFER    = 208,
};

[[nodiscard]] inline FieldID smf75_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    if (name == "SMF75INT")      return FieldID::INTERVAL_MS;
    if (name == "SMF75SAM")      return FieldID::SAMPLE_COUNT;
    if (name == "SMF75XNM")      return FieldID::SYSPLEX_NAME;

    if (name == "SMF75DSN")      return FieldID::DSN;
    if (name == "SMF75VOL")      return FieldID::VOLSER;
    if (name == "SMF75PST")      return FieldID::PST_FLAGS;
    if (name == "SMF75SLA")      return FieldID::TOTAL_SLOTS;
    if (name == "SMF75MXU")      return FieldID::MAX_USED;
    if (name == "SMF75MNU")      return FieldID::MIN_USED;
    if (name == "SMF75AVU")      return FieldID::AVG_USED;
    if (name == "SMF75SIO")      return FieldID::SIO_COUNT;
    if (name == "SMF75PGX")      return FieldID::PAGES_XFER;

    return FieldID::NONE;
}
} // namespace smf75

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf75Product {
    // RMF Product Section (SMF75PRO) — same layout pattern as SMF70/74.
    // TODO: verify field offsets within section against IBM GA32-0869 z/OS 3.1.
    mf::MfTime  interval_start_time{};  // SMF75IST: interval start time
    mf::MfDate  interval_start_date{};  // SMF75DAT: interval start date
    uint32_t    interval_hund{0};       // SMF75INT: duration (hundredths of seconds)
    uint16_t    sample_count{0};        // SMF75SAM: number of samples
    std::string sysplex_name;           // SMF75XNM: sysplex name (8 chars EBCDIC)
};

struct Smf75PageDs {
    // Page Data Set Section entry (SMF75PSD) — one per page dataset.
    // TODO: verify field byte offsets against IBM GA32-0869 z/OS 3.1.
    std::string dsn;             // SMF75DSN: page data set name (44 chars EBCDIC)
    std::string volume_serial;   // SMF75VOL: volume serial (6 chars EBCDIC)
    uint8_t     pst_flags{0};    // SMF75PST: page space type flags
    uint32_t    total_slots{0};  // SMF75SLA: total slots in data set
    uint32_t    max_used{0};     // SMF75MXU: max slots used
    uint32_t    min_used{0};     // SMF75MNU: min slots used
    uint32_t    avg_used{0};     // SMF75AVU: average slots used
    uint32_t    sio_count{0};    // SMF75SIO: number of SIOs to data set
    uint32_t    pages_xfer{0};   // SMF75PGX: pages transferred to data set

    [[nodiscard]] mf::FieldValue get_field(smf75::FieldID fid) const {
        using namespace smf75;
        switch (fid) {
            case FieldID::DSN:           return mf::FieldValue::from_string(dsn);
            case FieldID::VOLSER:        return mf::FieldValue::from_string(volume_serial);
            case FieldID::PST_FLAGS:     return mf::FieldValue::from_int64(pst_flags);
            case FieldID::TOTAL_SLOTS:   return mf::FieldValue::from_int64(total_slots);
            case FieldID::MAX_USED:      return mf::FieldValue::from_int64(max_used);
            case FieldID::MIN_USED:      return mf::FieldValue::from_int64(min_used);
            case FieldID::AVG_USED:      return mf::FieldValue::from_int64(avg_used);
            case FieldID::SIO_COUNT:     return mf::FieldValue::from_int64(sio_count);
            case FieldID::PAGES_XFER:    return mf::FieldValue::from_int64(pages_xfer);
            default:                     return mf::FieldValue::null();
        }
    }
};

struct Smf75Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf75Product               product;
    std::vector<Smf75PageDs>   pagesets;

    [[nodiscard]] mf::FieldValue get_field(smf75::FieldID fid) const {
        using namespace smf75;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            case FieldID::SAMPLE_COUNT:  return mf::FieldValue::from_int64(product.sample_count);
            case FieldID::SYSPLEX_NAME:  return mf::FieldValue::from_string(product.sysplex_name);
            
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf75Product read_smf75_product(mf::Reader& sr) {
    Smf75Product p;
    // RMF Product Section layout — same pattern as SMF70/74.
    // TODO: verify all offsets against IBM GA32-0869 for z/OS 3.1.
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // offset  0: SMF75MFV version (1 byte)
    sr.skip(8);                                // offset  1: SMF75PRD product name (8 bytes EBCDIC)
    p.interval_start_time = sr.read_mf_time(); // offset  9: SMF75IST (4 bytes)
    p.interval_start_date = sr.read_mf_date(); // offset 13: SMF75DAT (4 bytes)
    p.interval_hund       = sr.read_u32();     // offset 17: SMF75INT (4 bytes hundredths)
    p.sample_count        = sr.read_u16();     // offset 21: SMF75SAM (2 bytes)
    sr.skip(1);                                // offset 23: SMF75FLA flags
    if (sr.can_read(4))  sr.skip(4);          // offset 24: SMF75CYC sampling cycle
    if (sr.can_read(8))  sr.skip(8);          // offset 28: SMF75MVS software level
    if (sr.can_read(4))  sr.skip(4);          // offset 36: IML(1) PRF(1) PTN(1) SRL(1)
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8));  // offset 40: SMF75XNM TODO: verify
    return p;
}

[[nodiscard]] inline Smf75PageDs read_smf75_pageds(mf::Reader& er) {
    Smf75PageDs ps;
    // Page Data Set Section entry (SMF75PSD) sequential field read.
    // TODO: verify all field sizes and order against IBM GA32-0869 z/OS 3.1.
    // Field order follows IBM SMF Explorer reference doc for SMF75S1.
    if (er.can_read(44))
        ps.dsn = mf::rtrim(er.read_ebcdic(44));   // SMF75DSN: data set name (44 bytes)
    if (er.can_read(1))
        ps.pst_flags = er.read_u8();               // SMF75PST: page space type flags
    if (er.can_read(1)) er.skip(1);               // SMF75FL2: additional flags
    if (er.can_read(4)) er.skip(4);               // SMF75TYP: device type (4 bytes)
    if (er.can_read(4)) er.skip(4);               // SMF75CHA: device numbers (4 bytes)
    if (er.can_read(6))
        ps.volume_serial = mf::rtrim(er.read_ebcdic(6)); // SMF75VOL: VOLSER (6 bytes)
    if (er.can_read(1)) er.skip(1);               // SMF75SCS: subchannel set ID
    // Slot utilisation counts
    if (er.can_read(4))
        ps.total_slots = er.read_u32();            // SMF75SLA: total slots
    if (er.can_read(4))
        ps.max_used = er.read_u32();               // SMF75MXU: max slots used
    if (er.can_read(4))
        ps.min_used = er.read_u32();               // SMF75MNU: min slots used
    if (er.can_read(4))
        ps.avg_used = er.read_u32();               // SMF75AVU: avg slots used
    if (er.can_read(4)) er.skip(4);               // SMF75BDS: unusable slots
    if (er.can_read(4)) er.skip(4);               // SMF75USE: times DS in use by ASM
    if (er.can_read(4)) er.skip(4);               // SMF75REQ: total requests
    if (er.can_read(4))
        ps.sio_count = er.read_u32();              // SMF75SIO: SIOs to data set
    if (er.can_read(4))
        ps.pages_xfer = er.read_u32();             // SMF75PGX: pages transferred
    return ps;
}

/* ── Main SMF75 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf75Record read_smf75(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf75Record out;

    out.header  = mf::read_smf_header(r);  // advances to byte 20
    if (r.can_read(2)) out.subtype = r.read_u16();  // bytes 20-21 (absent on header-only records)

    // SMF75TRN: number of triplets (4 bytes at offset 22).
    // TODO: verify presence of SMF75TRN field vs. direct triplets from byte 22.
    [[maybe_unused]] const uint32_t trn = r.can_read(4) ? r.read_u32() : 0u;

    // Section pointer triplets from byte 26 (offset / length / count, 12 bytes each).
    const SectionPtr prod_ptr = read_section_ptr(r);  // [0] Product section
    const SectionPtr page_ptr = read_section_ptr(r);  // [1] Page data section

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf75_product(sr);

    // Page data section — one entry per page dataset; each becomes one row.
    if (page_ptr.count > 0 && page_ptr.length >= 72) {
        const std::size_t max_possible = (rec_bytes.size() > page_ptr.offset)
            ? (rec_bytes.size() - page_ptr.offset) / page_ptr.length
            : 0;
        const uint32_t actual_count = std::min(page_ptr.count, static_cast<uint32_t>(max_possible));

        if (actual_count > 0) {
            out.pagesets.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = page_ptr.offset + static_cast<std::size_t>(i) * page_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, page_ptr.length) };
                out.pagesets.push_back(read_smf75_pageds(er));
            }
        }
    }

    return out;
}

} // namespace smf
