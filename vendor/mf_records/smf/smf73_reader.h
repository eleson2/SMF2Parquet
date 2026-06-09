#pragma once
/*
 * smf/smf73_reader.h — SMF Type 73 record parser (parse core): RMF Channel Path Activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf73ChpidData {
    uint8_t     chpid{0};
    uint8_t     chpid_type{0};
    std::string chpid_acronym;
    uint32_t    busy_count{0};
    bool        is_valid{false};
};

struct Smf73Record {
    mf::SmfHeader          header;
    uint16_t               subtype{0};
    RmfProduct             product;
    std::vector<Smf73ChpidData> chpids;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf73ChpidData read_smf73_chpid(mf::Reader& er) {
    Smf73ChpidData d;
    if (er.can_read(1)) d.chpid = er.read_u8();
    if (er.can_read(1)) d.chpid_type = er.read_u8();
    if (er.can_read(1)) {
        uint8_t flags = er.read_u8();
        d.is_valid = (flags & 0x02);
    }
    er.skip(1);
    if (er.can_read(4)) d.busy_count = er.read_u32();
    er.skip(8);
    if (er.can_read(4)) d.chpid_acronym = mf::rtrim(er.read_ebcdic(4));
    return d;
}

/* ── Main SMF73 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf73Record read_smf73(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf73Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr hpd_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(hpd_ptr, rec_bytes.size());
    if (actual_count > 0 && hpd_ptr.len >= 20) {
        out.chpids.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = hpd_ptr.offset + static_cast<std::size_t>(i) * hpd_ptr.len;
            if (off + hpd_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, hpd_ptr.len) };
            auto chp = read_smf73_chpid(er);
            if (chp.is_valid) out.chpids.push_back(std::move(chp));
        }
    }

    return out;
}

} // namespace smf
