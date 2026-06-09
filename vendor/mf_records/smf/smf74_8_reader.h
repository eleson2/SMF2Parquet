#pragma once
/*
 * smf/smf74_8_reader.h — SMF Type 74 Subtype 8: RMF Enterprise Disk (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_8ExtentPool {
    uint32_t pool_id{0};     // R748XPID
    uint32_t pool_type{0};   // R748XPLT
    uint32_t real_cap_gb{0}; // R748XRCP
    uint32_t real_extents{0}; // R748XRNS
    uint32_t real_alloc{0};   // R748XRNA
};

struct Smf74_8Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    std::vector<Smf74_8ExtentPool> pools;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_8ExtentPool read_smf74_8_pool(mf::Reader& sr) {
    Smf74_8ExtentPool e;
    if (sr.can_read(4)) e.pool_id = sr.read_u32();
    if (sr.can_read(4)) e.pool_type = sr.read_u32();
    sr.skip(4);
    if (sr.can_read(12)) {
        e.real_cap_gb = sr.read_u32();
        e.real_extents = sr.read_u32();
        e.real_alloc   = sr.read_u32();
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
    const SectionPtr prod_ptr = read_section_ptr(r);
    r.skip(12 * 2);
    const SectionPtr pool_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(pool_ptr, rec_bytes.size());
    if (actual_count > 0 && pool_ptr.len >= 24) {
        out.pools.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = pool_ptr.offset + static_cast<std::size_t>(i) * pool_ptr.len;
            if (off + pool_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, pool_ptr.len) };
            out.pools.push_back(read_smf74_8_pool(er));
        }
    }

    return out;
}

} // namespace smf
