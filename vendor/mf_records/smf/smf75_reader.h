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
};

struct Smf75Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf75Product               product;
    std::vector<Smf75PageDs>   pagesets;
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
        out.pagesets.reserve(page_ptr.count);
        for (uint32_t i = 0; i < page_ptr.count; ++i) {
            const std::size_t off = page_ptr.offset + static_cast<std::size_t>(i) * page_ptr.length;
            if (off + page_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, page_ptr.length) };
            out.pagesets.push_back(read_smf75_pageds(er));
        }
    }

    return out;
}

} // namespace smf
