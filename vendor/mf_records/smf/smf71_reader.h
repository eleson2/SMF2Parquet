#pragma once
/*
 * smf/smf71_reader.h — SMF Type 71 record parser (parse core): RMF Paging Activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf71PagingData {
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
    RmfProduct      product;
    Smf71PagingData paging;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline SectionPtr read_rmf_triplet_422(mf::Reader& r) {
    SectionPtr p;
    if (!r.can_read(8)) return p;
    p.offset = r.read_u32();
    p.len    = r.read_u16();
    p.count  = r.read_u16();
    return p;
}

[[nodiscard]] inline Smf71PagingData read_smf71_paging(mf::Reader& sr) {
    Smf71PagingData d;
    if (sr.can_read(4)) d.total_page_ins = sr.read_u32();
    if (sr.can_read(4)) d.total_page_outs = sr.read_u32();
    sr.skip(8);
    if (sr.can_read(4)) d.avg_avail_frames = sr.read_u32();
    if (sr.can_read(4)) d.max_avail_frames = sr.read_u32();
    if (sr.can_read(4)) d.min_avail_frames = sr.read_u32();
    sr.skip(4);
    if (sr.can_read(4)) d.fixed_frames = sr.read_u32();
    return d;
}

/* ── Main SMF71 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf71Record read_smf71(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf71Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 24;
    const SectionPtr prod_ptr = read_rmf_triplet_422(r);
    const SectionPtr pag_ptr  = read_rmf_triplet_422(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, pag_ptr, 32, sr))
        out.paging = read_smf71_paging(sr);

    return out;
}

} // namespace smf
