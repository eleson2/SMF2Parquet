#pragma once
/*
 * smf/smf70_reader.h — SMF Type 70 Subtype 1: RMF CPU Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf70Control {
    std::string cpc_model;
    uint16_t    zaap_online{0};
    uint16_t    ziip_online{0};
};

struct Smf70CpuData {
    uint8_t     cpu_type{0};     // 0=CP, 2=zIIP
    uint32_t    wait_hund{0};
    uint32_t    parked_hund{0};
};

struct Smf70Record {
    mf::SmfHeader  header;
    uint16_t       subtype{0};
    RmfProduct     product;
    Smf70Control   control;
    
    uint32_t cp_count{0};
    uint64_t cp_wait_hund{0};
    uint64_t cp_parked_hund{0};
    
    uint32_t ziip_lp_count{0};
    uint64_t ziip_wait_hund{0};
    uint64_t ziip_parked_hund{0};
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf70Control read_smf70_control(mf::Reader& sr) {
    Smf70Control c;
    if (sr.can_read(4))
        c.cpc_model = mf::rtrim(sr.read_ebcdic(4));
    if (sr.can_read(22 + 2)) {
        sr.pos = 22;
        c.zaap_online = sr.read_u16();
        c.ziip_online = sr.read_u16();
    }
    return c;
}

[[nodiscard]] inline Smf70CpuData read_smf70_cpu(mf::Reader& er) {
    Smf70CpuData d;
    if (er.can_read(1)) d.cpu_type = er.read_u8();
    if (er.can_read(4 + 4)) {
        er.pos = 4;
        d.wait_hund   = er.read_u32();
        d.parked_hund = er.read_u32();
    }
    return d;
}

/* ── Main SMF70 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf70Record read_smf70(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf70Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr cpu_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, ctrl_ptr, 16, sr))
        out.control = read_smf70_control(sr);

    const uint32_t n_cpu = safe_count(cpu_ptr, rec_bytes.size());
    if (n_cpu > 0 && cpu_ptr.len >= 16) {
        for (uint32_t i = 0; i < n_cpu; ++i) {
            const std::size_t off = cpu_ptr.offset + static_cast<std::size_t>(i) * cpu_ptr.len;
            if (off + cpu_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, cpu_ptr.len) };
            auto cpu = read_smf70_cpu(er);
            
            if (cpu.cpu_type == 0) {
                ++out.cp_count;
                out.cp_wait_hund   += cpu.wait_hund;
                out.cp_parked_hund += cpu.parked_hund;
            } else if (cpu.cpu_type == 2) {
                ++out.ziip_lp_count;
                out.ziip_wait_hund   += cpu.wait_hund;
                out.ziip_parked_hund += cpu.parked_hund;
            }
        }
    }

    return out;
}

} // namespace smf
