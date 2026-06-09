#pragma once
/*
 * smf/smf74_6_reader.h — SMF Type 74 Subtype 6: RMF VTS Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_6GlobalData {
    uint32_t max_virtual_mb{0};  // R746GMXV
    uint32_t inuse_virtual_pages{0}; // R746GUSV
    uint32_t min_fixed_mb{0};    // R746GMNF
    uint32_t inuse_fixed_pages{0}; // R746GUSF
    double   metadata_hits{0};   // R746GMC
    double   metadata_misses{0}; // R746GMNC
};

struct Smf74_6Record {
    mf::SmfHeader     header;
    uint16_t          subtype{0};
    RmfProduct        product;
    Smf74_6GlobalData global;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_6GlobalData read_smf74_6_global(mf::Reader& sr) {
    Smf74_6GlobalData d;
    if (sr.can_read(4)) d.max_virtual_mb = sr.read_u32();
    if (sr.can_read(4)) d.inuse_virtual_pages = sr.read_u32();
    if (sr.can_read(4)) d.min_fixed_mb = sr.read_u32();
    if (sr.can_read(4)) d.inuse_fixed_pages = sr.read_u32();
    if (sr.can_read(8)) d.metadata_hits = sr.read_comp2();
    if (sr.can_read(8)) d.metadata_misses = sr.read_comp2();
    return d;
}

/* ── Main SMF74-6 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_6Record read_smf74_6(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_6Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 6) return out;

    r.pos = 26;
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr glob_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, glob_ptr, 16, sr))
        out.global = read_smf74_6_global(sr);

    return out;
}

} // namespace smf
