#pragma once
/*
 * smf/smf74_5_reader.h — SMF Type 74 Subtype 5: RMF Cache Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_5CacheDev {
    std::string volser;      // R745DVOL
    uint16_t    device_num;  // R745DEVN
    uint32_t    read_req{0}; // R745DRCR
    uint32_t    read_hit{0}; // R745DCRH
    uint32_t    write_req{0};// R745DWRC
    uint32_t    write_hit{0};// R745DWCH
};

struct Smf74_5Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    std::vector<Smf74_5CacheDev> devices;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_5CacheDev read_smf74_5_dev(mf::Reader& sr) {
    Smf74_5CacheDev d;
    if (sr.can_read(6)) d.volser = mf::rtrim(sr.read_ebcdic(6));
    sr.skip(4);
    if (sr.can_read(2)) d.device_num = sr.read_u16();
    sr.skip(6);
    if (sr.can_read(4)) d.read_req  = sr.read_u32();
    if (sr.can_read(4)) d.read_hit  = sr.read_u32();
    if (sr.can_read(4)) d.write_req = sr.read_u32();
    if (sr.can_read(4)) d.write_hit = sr.read_u32();
    return d;
}

/* ── Main SMF74-5 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_5Record read_smf74_5(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_5Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 5) return out;

    r.pos = 26;
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr dev_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(dev_ptr, rec_bytes.size());
    if (actual_count > 0 && dev_ptr.len >= 32) {
        out.devices.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = dev_ptr.offset + static_cast<std::size_t>(i) * dev_ptr.len;
            if (off + dev_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, dev_ptr.len) };
            out.devices.push_back(read_smf74_5_dev(er));
        }
    }

    return out;
}

} // namespace smf
