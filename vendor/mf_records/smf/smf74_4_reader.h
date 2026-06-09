#pragma once
/*
 * smf/smf74_4_reader.h — SMF Type 74 Subtype 4: RMF Coupling Facility Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_4CfData {
    std::string cf_name;    // R744FNAM
    uint16_t    cf_level{0}; // R744FLVL
    uint32_t    total_req{0}; // R744FTOR
};

struct Smf74_4Structure {
    std::string structure_name; // R744QSTR
    uint32_t    size_4k{0};      // R744QSIZ
};

struct Smf74_4Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    Smf74_4CfData              cf;
    std::vector<Smf74_4Structure> structures;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_4CfData read_smf74_4_cf(mf::Reader& sr) {
    Smf74_4CfData c;
    if (sr.can_read(8)) c.cf_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(52 + 4)) {
        sr.pos = 52;
        c.total_req = sr.read_u32();
    }
    if (sr.can_read(101 + 1)) {
        sr.pos = 101;
        c.cf_level = sr.read_u8();
    }
    return c;
}

[[nodiscard]] inline Smf74_4Structure read_smf74_4_structure(mf::Reader& sr) {
    Smf74_4Structure s;
    if (sr.can_read(16)) s.structure_name = mf::rtrim(sr.read_ebcdic(16));
    if (sr.can_read(16 + 4)) {
        sr.pos = 16;
        s.size_4k = sr.read_u32();
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
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr cf_ptr   = read_section_ptr(r);
    r.skip(12 * 2);
    const SectionPtr str_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, cf_ptr, 8, sr))
        out.cf = read_smf74_4_cf(sr);

    const uint32_t actual_count = safe_count(str_ptr, rec_bytes.size());
    if (actual_count > 0 && str_ptr.len >= 20) {
        out.structures.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = str_ptr.offset + static_cast<std::size_t>(i) * str_ptr.len;
            if (off + str_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, str_ptr.len) };
            out.structures.push_back(read_smf74_4_structure(er));
        }
    }

    return out;
}

} // namespace smf
