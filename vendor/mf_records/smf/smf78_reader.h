#pragma once
/*
 * smf/smf78_reader.h — SMF Type 78 record parser (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf78_3LcuData {
    uint16_t lcu_id{0};          // R783RLCU
    uint8_t  css_id{0};          // R783CSSI
    uint32_t queue_len_sum{0};   // R783QLNS
    uint32_t queue_len_count{0}; // R783QLNC
};

struct Smf78Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    RmfProduct               product;
    std::vector<Smf78_3LcuData> lcus;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf78_3LcuData read_smf78_3_lcu(mf::Reader& er) {
    Smf78_3LcuData d;
    if (er.can_read(2)) d.lcu_id = er.read_u16();
    if (er.can_read(1)) d.css_id = er.read_u8();
    if (er.can_read(13 + 4)) {
        er.pos = 16;
        d.queue_len_sum = er.read_u32();
    }
    if (er.can_read(20 + 4)) {
        er.pos = 20;
        d.queue_len_count = er.read_u32();
    }
    return d;
}

/* ── Main SMF78 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf78Record read_smf78(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf78Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out;

    r.pos = 28; // Triplets start at 28
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr conf_ptr = read_section_ptr(r);
    const SectionPtr lcu_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(lcu_ptr, rec_bytes.size());
    if (actual_count > 0 && lcu_ptr.len >= 32) {
        out.lcus.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = lcu_ptr.offset + static_cast<std::size_t>(i) * lcu_ptr.len;
            if (off + lcu_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, lcu_ptr.len) };
            out.lcus.push_back(read_smf78_3_lcu(er));
        }
    }

    return out;
}

} // namespace smf
