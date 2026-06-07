#pragma once
/*
 * smf/smf74_5_reader.h — SMF Type 74 Subtype 5: RMF Cache Activity (parse core).
 *
 * One record per RMF measurement interval per subsystem. Contains multiple
 * device entries. Each device becomes one output row.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 5)
 *   Offset 22-25  SMF74TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF74PRS)
 *   [1] Cache Control        (SMF745CO)
 *   [2] Cache Device Data    (SMF745DO) — one entry per device
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

struct Smf74_5Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_5CacheDev {
    std::string volser;      // R745DVOL (6 bytes)
    uint16_t    device_num;  // R745DEVN (2 bytes)
    uint32_t    read_req{0}; // R745DRCR (4 bytes assumed)
    uint32_t    read_hit{0}; // R745DCRH
    uint32_t    write_req{0};// R745DWRC
    uint32_t    write_hit{0};// R745DWCH
};

struct Smf74_5Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf74_5Product             product;
    std::vector<Smf74_5CacheDev> devices;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_5Product read_smf74_5_product(mf::Reader& sr) {
    Smf74_5Product p;
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

[[nodiscard]] inline Smf74_5CacheDev read_smf74_5_dev(mf::Reader& sr) {
    Smf74_5CacheDev d;
    if (sr.can_read(6)) d.volser = mf::rtrim(sr.read_ebcdic(6)); // R745DVOL
    sr.skip(4); // FL4, SCS, CCU, UNT
    if (sr.can_read(2)) d.device_num = sr.read_u16(); // R745DEVN
    sr.skip(6); // FLG, PDF, DVID, VS1, SDP, VS2
    // Counters start at offset 18 in section
    if (sr.can_read(4)) d.read_req  = sr.read_u32(); // R745DRCR
    if (sr.can_read(4)) d.read_hit  = sr.read_u32(); // R745DCRH
    if (sr.can_read(4)) d.write_req = sr.read_u32(); // R745DWRC
    if (sr.can_read(4)) d.write_hit = sr.read_u32(); // R745DWCH
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
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Control
    const SectionPtr dev_ptr  = read_section_ptr(r); // [2] Device Data

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_5_product(sr);

    if (dev_ptr.count > 0 && dev_ptr.length >= 32) {
        out.devices.reserve(dev_ptr.count);
        for (uint32_t i = 0; i < dev_ptr.count; ++i) {
            const std::size_t off = dev_ptr.offset + static_cast<std::size_t>(i) * dev_ptr.length;
            if (off + dev_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, dev_ptr.length) };
            out.devices.push_back(read_smf74_5_dev(er));
        }
    }

    return out;
}

} // namespace smf
