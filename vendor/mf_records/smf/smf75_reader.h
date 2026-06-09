#pragma once
/*
 * smf/smf75_reader.h — SMF Type 75 Subtype 1: RMF Page Data Set Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf75PageDs {
    std::string dsn;            // SMF75DSN
    std::string volume_serial;  // SMF75SER
    uint16_t    pst_flags{0};   // SMF75FLG
    uint32_t    total_slots{0}; // SMF75SLT
    uint32_t    max_used{0};    // SMF75MXU
    uint32_t    min_used{0};    // SMF75MNU
    uint32_t    avg_used{0};    // SMF75AVU
    uint32_t    sio_count{0};   // SMF75SIO
    uint32_t    pages_xfer{0};  // SMF75TXF
};

struct Smf75Record {
    mf::SmfHeader   header;
    uint16_t        subtype{0};
    RmfProduct      product;
    std::vector<Smf75PageDs> pagesets;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf75PageDs read_smf75_pageds(mf::Reader& er) {
    Smf75PageDs d;
    if (er.can_read(44)) d.dsn = mf::rtrim(er.read_ebcdic(44));
    if (er.can_read(6))  d.volume_serial = mf::rtrim(er.read_ebcdic(6));
    if (er.can_read(2))  d.pst_flags = er.read_u16();
    if (er.can_read(4))  d.total_slots = er.read_u32();
    if (er.can_read(4))  d.max_used    = er.read_u32();
    if (er.can_read(4))  d.min_used    = er.read_u32();
    if (er.can_read(4))  d.avg_used    = er.read_u32();
    if (er.can_read(4))  d.sio_count   = er.read_u32();
    if (er.can_read(4))  d.pages_xfer  = er.read_u32();
    return d;
}

/* ── Main SMF75 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf75Record read_smf75(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf75Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr page_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(page_ptr, rec_bytes.size());
    if (actual_count > 0 && page_ptr.len >= 80) {
        out.pagesets.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = page_ptr.offset + static_cast<std::size_t>(i) * page_ptr.len;
            if (off + page_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, page_ptr.len) };
            out.pagesets.push_back(read_smf75_pageds(er));
        }
    }

    return out;
}

} // namespace smf
