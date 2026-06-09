#pragma once
/*
 * smf/smf74_9_reader.h — SMF Type 74 Subtype 9: RMF PCIE Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_9PcieFunc {
    uint32_t    pfid{0};         // R749PFID
    std::string job_name;        // R749JOBN
    uint16_t    asid{0};         // R749ASID
    uint8_t     func_type{0};    // R749PFT
    uint64_t    load_ops{0};     // R749LOOP
    uint64_t    store_ops{0};    // R749STOP
};

struct Smf74_9Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    std::vector<Smf74_9PcieFunc> functions;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_9PcieFunc read_smf74_9_func(mf::Reader& sr) {
    Smf74_9PcieFunc f;
    if (sr.can_read(4)) f.pfid = sr.read_u32();
    if (sr.can_read(28 + 8)) {
        sr.pos = 28;
        f.job_name = mf::rtrim(sr.read_ebcdic(8));
    }
    if (sr.can_read(36 + 2)) {
        sr.pos = 36;
        f.asid = sr.read_u16();
    }
    if (sr.can_read(56 + 8)) {
        sr.pos = 56;
        f.load_ops = sr.read_u64();
    }
    if (sr.can_read(64 + 8)) {
        sr.pos = 64;
        f.store_ops = sr.read_u64();
    }
    if (sr.can_read(114 + 1)) {
        sr.pos = 114;
        f.func_type = sr.read_u8();
    }
    return f;
}

/* ── Main SMF74-9 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_9Record read_smf74_9(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_9Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 9) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr func_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(func_ptr, rec_bytes.size());
    if (actual_count > 0 && func_ptr.len >= 120) {
        out.functions.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = func_ptr.offset + static_cast<std::size_t>(i) * func_ptr.len;
            if (off + func_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, func_ptr.len) };
            out.functions.push_back(read_smf74_9_func(er));
        }
    }

    return out;
}

} // namespace smf
