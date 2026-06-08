#pragma once
/*
 * smf/smf74_9_reader.h — SMF Type 74 Subtype 9: RMF PCIE Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains multiple
 * PCIE function entries. Each function becomes one output row.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 9)
 *   Offset 22-25  SMF74TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF74PRS)
 *   [1] PCIE Function Data   (SMF749PO) — one entry per function
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

struct Smf74_9Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_9PcieFunc {
    uint32_t    pfid{0};         // R749PFID (4 bytes)
    std::string job_name;        // R749JOBN (8 bytes)
    uint16_t    asid{0};         // R749ASID (2 bytes)
    uint8_t     func_type{0};    // R749PFT  (offset 114)
    uint64_t    load_ops{0};     // R749LOOP (8 bytes, offset 56)
    uint64_t    store_ops{0};    // R749STOP (8 bytes, offset 64)
};

struct Smf74_9Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf74_9Product             product;
    std::vector<Smf74_9PcieFunc> functions;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_9Product read_smf74_9_product(mf::Reader& sr) {
    Smf74_9Product p;
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
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr func_ptr = read_section_ptr(r); // [1] PCIE Function

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_9_product(sr);

    if (func_ptr.length >= 120) {
        const uint32_t actual_count = func_ptr.safe_count(rec_bytes.size());
        if (actual_count > 0) {
            out.functions.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = func_ptr.offset + static_cast<std::size_t>(i) * func_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, func_ptr.length) };
                out.functions.push_back(read_smf74_9_func(er));
            }
        }
    }

    return out;
}

} // namespace smf
