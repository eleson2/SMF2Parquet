#pragma once
/*
 * smf/smf74_6_reader.h — SMF Type 74 Subtype 6: RMF VTS Activity (parse core).
 *
 * One record per RMF measurement interval per system.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S6/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_6Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_6GlobalData {
    uint32_t max_virtual_mb{0};  // R746GMXV
    uint32_t inuse_virtual_pages{0}; // R746GUSV
    uint32_t min_fixed_mb{0};    // R746GMNF
    uint32_t inuse_fixed_pages{0}; // R746GUSF
    double   metadata_hits{0};   // R746GMC (IBM Float 8)
    double   metadata_misses{0}; // R746GMNC
};

struct Smf74_6Record {
    mf::SmfHeader     header;
    uint16_t          subtype{0};
    Smf74_6Product    product;
    Smf74_6GlobalData global;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_6Product read_smf74_6_product(mf::Reader& sr) {
    Smf74_6Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(9);
    p.interval_start_time = sr.read_mf_time();
    p.interval_start_date = sr.read_mf_date();
    p.interval_hund       = sr.read_u32();
    p.sample_count        = sr.read_u16();
    sr.skip(18);
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8));
    return p;
}

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
        out.product = read_smf74_6_product(sr);

    if (make_section_reader(rec_bytes, glob_ptr, 16, sr))
        out.global = read_smf74_6_global(sr);

    return out;
}

} // namespace smf
