#pragma once
/*
 * smf/smf74_8_reader.h — SMF Type 74 Subtype 8: RMF Enterprise Disk (parse core).
 *
 * One record per RMF measurement interval per subsystem. Contains multiple
 * extent pool entries. Each extent pool becomes one output row.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 8)
 *   Offset 22-25  SMF74TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF74PRS)
 *   [1] Link Control         (SMF748CO)
 *   [2] Link Data            (SMF748LO)
 *   [3] Extent Pool Data     (SMF748XO) — one entry per pool
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

struct Smf74_8Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_8ExtentPool {
    uint32_t pool_id{0};     // R748XPID (4 bytes)
    uint32_t pool_type{0};   // R748XPLT (4 bytes)
    uint32_t real_cap_gb{0}; // R748XRCP (offset 12)
    uint32_t real_extents{0}; // R748XRNS (offset 16)
    uint32_t real_alloc{0};   // R748XRNA (offset 20)
};

struct Smf74_8Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf74_8Product             product;
    std::vector<Smf74_8ExtentPool> pools;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_8Product read_smf74_8_product(mf::Reader& sr) {
    Smf74_8Product p;
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

[[nodiscard]] inline Smf74_8ExtentPool read_smf74_8_pool(mf::Reader& sr) {
    Smf74_8ExtentPool e;
    if (sr.can_read(4)) e.pool_id = sr.read_u32();
    if (sr.can_read(4)) e.pool_type = sr.read_u32();
    sr.skip(4); // R748XPTQ
    if (sr.can_read(12)) {
        e.real_cap_gb = sr.read_u32(); // R748XRCP
        e.real_extents = sr.read_u32(); // R748XRNS
        e.real_alloc   = sr.read_u32(); // R748XRNA
    }
    return e;
}

/* ── Main SMF74-8 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_8Record read_smf74_8(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_8Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 8) return out;

    r.pos = 26;
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    r.skip(12 * 2);                                  // [1] Link Control, [2] Link Data
    const SectionPtr pool_ptr = read_section_ptr(r); // [3] Extent Pool

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_8_product(sr);

    if (pool_ptr.count > 0 && pool_ptr.length >= 24) {
        out.pools.reserve(pool_ptr.count);
        for (uint32_t i = 0; i < pool_ptr.count; ++i) {
            const std::size_t off = pool_ptr.offset + static_cast<std::size_t>(i) * pool_ptr.length;
            if (off + pool_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, pool_ptr.length) };
            out.pools.push_back(read_smf74_8_pool(er));
        }
    }

    return out;
}

} // namespace smf
