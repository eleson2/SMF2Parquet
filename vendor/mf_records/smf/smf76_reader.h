#pragma once
/*
 * smf/smf76_reader.h — SMF Type 76 record parser (parse core): RMF storage activity.
 *
 * Type 76 records describe real and virtual storage activity: page-in/page-out
 * rates, frame counts, working set sizes, and swap activity.
 *
 * Subtypes:
 *   1  Paging and swapping activity
 *   2  Segment and page table activity
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf76Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

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
    Smf76Product   product;
    Smf76_1Paging  paging;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf76Product read_smf76_product(mf::Reader& sr) {
    Smf76Product p;
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
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Monitor II Control
    const SectionPtr data_ptr = read_section_ptr(r); // [2] Paging Data

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf76_product(sr);

    if (make_section_reader(rec_bytes, data_ptr, 24, sr))
        out.paging = read_smf76_1_paging(sr);

    return out;
}

} // namespace smf
