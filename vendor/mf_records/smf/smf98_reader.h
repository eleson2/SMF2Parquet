#pragma once
/*
 * smf/smf98_reader.h — SMF Type 98 Subtype 1: z/OS Supervisor Activity (parse core).
 */

#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Triplet8 {
    uint32_t offset{0};
    uint16_t length{0};
    uint16_t count{0};

    [[nodiscard]] uint32_t safe_count(std::size_t rec_size) const noexcept {
        if (count == 0 || length == 0 || offset >= rec_size) return 0;
        const std::size_t remaining = rec_size - offset;
        const std::size_t max_possible = remaining / length;
        return (count < max_possible) ? count : static_cast<uint32_t>(max_possible);
    }
};

[[nodiscard]] inline Triplet8 read_triplet_8(mf::Reader& r) {
    if (!r.can_read(8)) return Triplet8{};
    Triplet8 t;
    t.offset = r.read_u32();
    t.length = r.read_u16();
    t.count  = r.read_u16();
    return t;
}

struct Smf98Consumption {
    uint16_t    asid{0};
    std::string job_name;
};

struct Smf98Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    std::vector<Smf98Consumption> as_consumption;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf98Consumption read_smf98_consume(mf::Reader& sr) {
    Smf98Consumption c;
    if (sr.can_read(2)) c.asid = sr.read_u16();
    if (sr.can_read(2 + 8)) {
        sr.pos = 2;
        c.job_name = mf::rtrim(sr.read_ebcdic(8));
    }
    return c;
}

/* ── Main SMF98 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf98Record read_smf98(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf98Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 28;
    uint16_t num_triplets = r.can_read(2) ? r.read_u16() : 0;
    
    r.pos = 32;
    if (num_triplets < 12) return out;

    for (int i = 0; i < 11; ++i) (void)read_triplet_8(r);
    const Triplet8 cons_ptr = read_triplet_8(r);

    const uint32_t actual_count = cons_ptr.safe_count(rec_bytes.size());
    if (actual_count > 0 && cons_ptr.length >= 10) {
        out.as_consumption.reserve(actual_count);
        for (uint16_t i = 0; i < actual_count; ++i) {
            const std::size_t off = cons_ptr.offset + static_cast<std::size_t>(i) * cons_ptr.length;
            if (off + cons_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, cons_ptr.length) };
            out.as_consumption.push_back(read_smf98_consume(er));
        }
    }

    return out;
}

} // namespace smf
