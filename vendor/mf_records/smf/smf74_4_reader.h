#pragma once
/*
 * smf/smf74_4_reader.h — SMF Type 74 Subtype 4: RMF Coupling Facility Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains multiple
 * structure entries. Each structure becomes one output row.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 4)
 *   Offset 22-25  SMF74TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard order)
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF74PRS)
 *   [1] Local CF Data        (SMF744FO)
 *   [2] Connectivity Data    (SMF744XO)
 *   [3] Storage Data         (SMF744GO)
 *   [4] Structure Data       (SMF744QO) — one entry per structure
 *   [5] Request Data         (SMF744SO) — one entry per structure
 *   ...
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S4/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_4Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_4CfData {
    std::string cf_name;    // R744FNAM (8 bytes)
    uint16_t    cf_level{0}; // R744FLVL (offset 101)
    uint32_t    total_req{0}; // R744FTOR (offset 52)
};

struct Smf74_4Structure {
    std::string structure_name; // R744QSTR (16 bytes)
    uint32_t    size_4k{0};      // R744QSIZ (offset 16)
};

struct Smf74_4Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf74_4Product             product;
    Smf74_4CfData              cf;
    std::vector<Smf74_4Structure> structures;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_4Product read_smf74_4_product(mf::Reader& sr) {
    Smf74_4Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(9);                                // version + product
    p.interval_start_time = sr.read_mf_time(); // SMF74IST
    p.interval_start_date = sr.read_mf_date(); // SMF74DAT
    p.interval_hund       = sr.read_u32();     // SMF74INT
    p.sample_count        = sr.read_u16();     // SMF74SAM
    sr.skip(18);                               // flags + cyc + mvs + srl
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF74XNM
    return p;
}

[[nodiscard]] inline Smf74_4CfData read_smf74_4_cf(mf::Reader& sr) {
    Smf74_4CfData c;
    if (sr.can_read(8)) c.cf_name = mf::rtrim(sr.read_ebcdic(8)); // R744FNAM
    if (sr.can_read(52 + 4)) {
        sr.pos = 52;
        c.total_req = sr.read_u32(); // R744FTOR
    }
    if (sr.can_read(101 + 1)) {
        sr.pos = 101;
        c.cf_level = sr.read_u8(); // R744FLVL
    }
    return c;
}

[[nodiscard]] inline Smf74_4Structure read_smf74_4_structure(mf::Reader& sr) {
    Smf74_4Structure s;
    if (sr.can_read(16)) s.structure_name = mf::rtrim(sr.read_ebcdic(16)); // R744QSTR
    if (sr.can_read(16 + 4)) {
        sr.pos = 16;
        s.size_4k = sr.read_u32(); // R744QSIZ
    }
    return s;
}

/* ── Main SMF74-4 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_4Record read_smf74_4(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_4Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 4) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr cf_ptr   = read_section_ptr(r); // [1] Local CF
    r.skip(12 * 2);                                  // [2] Connectivity, [3] Storage
    const SectionPtr str_ptr  = read_section_ptr(r); // [4] Structure

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_4_product(sr);

    if (make_section_reader(rec_bytes, cf_ptr, 8, sr))
        out.cf = read_smf74_4_cf(sr);

    if (str_ptr.length >= 20) {
        const uint32_t actual_count = str_ptr.safe_count(rec_bytes.size());
        if (actual_count > 0) {
            out.structures.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = str_ptr.offset + static_cast<std::size_t>(i) * str_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, str_ptr.length) };
                out.structures.push_back(read_smf74_4_structure(er));
            }
        }
    }

    return out;
}

} // namespace smf
