#pragma once
/*
 * smf/smf70_2_reader.h — SMF Type 70 Subtype 2: RMF Cryptographic Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains multiple
 * crypto processor entries. Each entry becomes one output row.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 2)
 *   Offset 22-25  SMF70TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF70PRS)
 *   [1] CCA Coprocessor      (SMF7023S) — one entry per card
 *   ...
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf70_2Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf70_2CryptoCard {
    uint16_t card_index{0};  // r7023ax (2 bytes)
    uint16_t card_type{0};   // r7023ct (2 bytes)
    double   ops_count{0};   // r7023c0 (8-byte float assumed)
    double   exec_time{0};   // r7023t0 (8-byte float assumed)
};

struct Smf70_2Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf70_2Product             product;
    std::vector<Smf70_2CryptoCard> cards;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf70_2Product read_smf70_2_product(mf::Reader& sr) {
    Smf70_2Product p;
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

[[nodiscard]] inline Smf70_2CryptoCard read_smf70_2_card(mf::Reader& sr) {
    Smf70_2CryptoCard c;
    if (sr.can_read(2)) c.card_index = sr.read_u16();
    if (sr.can_read(2)) c.card_type = sr.read_u16();
    sr.skip(12); // Mask(8), Scaling(8) -- Actually Scaling is a double, Mask is 8 bytes
    if (sr.can_read(16)) {
        c.exec_time = sr.read_comp2(); // r7023t0
        c.ops_count = sr.read_comp2(); // r7023c0
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
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr card_ptr = read_section_ptr(r); // [1] CCA Coprocessor

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf70_2_product(sr);

    if (card_ptr.count > 0 && card_ptr.length >= 32) {
        out.cards.reserve(card_ptr.count);
        for (uint32_t i = 0; i < card_ptr.count; ++i) {
            const std::size_t off = card_ptr.offset + static_cast<std::size_t>(i) * card_ptr.length;
            if (off + card_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, card_ptr.length) };
            out.cards.push_back(read_smf70_2_card(er));
        }
    }

    return out;
}

} // namespace smf
