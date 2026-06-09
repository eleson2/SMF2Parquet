#pragma once
/*
 * smf/smf76_reader.h — SMF Type 76 record parser (parse core): RMF storage activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf76_1Paging {
    uint32_t page_ins{0};      // R761PIN
    uint32_t page_outs{0};     // R761POT
    uint32_t swap_ins{0};      // R761SLC
    uint32_t swap_outs{0};     // R761SOC
    uint32_t frame_count{0};   // R761FMCT
    uint32_t working_set{0};   // R761WSS
};

struct Smf76Record {
    mf::SmfHeader  header;
    uint16_t       subtype{0};
    RmfProduct     product;
    Smf76_1Paging  paging;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf76_1Paging read_smf76_1_paging(mf::Reader& sr) {
    Smf76_1Paging p;
    if (sr.can_read(24)) {
        p.page_ins    = sr.read_u32(); // R761PIN
        p.page_outs   = sr.read_u32(); // R761POT
        p.swap_ins    = sr.read_u32(); // R761SLC
        p.swap_outs   = sr.read_u32(); // R761SOC
        p.frame_count = sr.read_u32(); // R761FMCT
        p.working_set = sr.read_u32(); // R761WSS
    }
    return p;
}

/* ── Main SMF76 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf76Record read_smf76(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf76Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const mf::Triplet prod_ptr = mf::read_triplet(r);
    const mf::Triplet ctrl_ptr = mf::read_triplet(r);
    const mf::Triplet data_ptr = mf::read_triplet(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, data_ptr, 24, sr))
        out.paging = read_smf76_1_paging(sr);

    return out;
}

} // namespace smf
