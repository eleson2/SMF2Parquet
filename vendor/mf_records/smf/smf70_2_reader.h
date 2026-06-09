#pragma once
/*
 * smf/smf70_2_reader.h — SMF Type 70 Subtype 2: RMF Cryptographic Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf70_2CryptoCard {
    uint16_t card_index{0};  // r7023ax
    uint16_t card_type{0};   // r7023ct
    double   ops_count{0};   // r7023c0
    double   exec_time{0};   // r7023t0
};

struct Smf70_2Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    std::vector<Smf70_2CryptoCard> cards;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf70_2CryptoCard read_smf70_2_card(mf::Reader& sr) {
    Smf70_2CryptoCard c;
    if (sr.can_read(2)) c.card_index = sr.read_u16();
    if (sr.can_read(2)) c.card_type = sr.read_u16();
    sr.skip(12);
    if (sr.can_read(16)) {
        c.exec_time = sr.read_comp2();
        c.ops_count = sr.read_comp2();
    }
    return c;
}

/* ── Main SMF70-2 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf70_2Record read_smf70_2(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf70_2Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 2) return out;

    r.pos = 26;
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr card_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(card_ptr, rec_bytes.size());
    if (actual_count > 0 && card_ptr.len >= 32) {
        out.cards.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = card_ptr.offset + static_cast<std::size_t>(i) * card_ptr.len;
            if (off + card_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, card_ptr.len) };
            out.cards.push_back(read_smf70_2_card(er));
        }
    }

    return out;
}

} // namespace smf
