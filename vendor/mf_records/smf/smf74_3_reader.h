#pragma once
/*
 * smf/smf74_3_reader.h — SMF Type 74 Subtype 3: RMF OMVS Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_3OmvsData {
    uint32_t syscall_count{0};    // R743SYSC
    uint32_t syscall_cpu_ms{0};   // R743CPU
    uint32_t max_processes{0};    // R743MAXP
    uint32_t max_users{0};        // R743MAXU
    uint32_t current_processes{0};// R743CURP
    uint32_t current_users{0};    // R743CURU
};

struct Smf74_3Record {
    mf::SmfHeader  header;
    uint16_t       subtype{0};
    RmfProduct     product;
    Smf74_3OmvsData omvs;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_3OmvsData read_smf74_3_omvs(mf::Reader& sr) {
    Smf74_3OmvsData d;
    if (sr.can_read(12)) sr.skip(12);
    if (sr.can_read(4)) d.syscall_count = sr.read_u32();
    sr.skip(8);
    if (sr.can_read(4)) d.syscall_cpu_ms = sr.read_u32();
    sr.skip(40);
    if (sr.can_read(4)) d.max_processes = sr.read_u32();
    if (sr.can_read(4)) d.max_users = sr.read_u32();
    sr.skip(4);
    if (sr.can_read(4)) d.current_processes = sr.read_u32();
    sr.skip(8);
    if (sr.can_read(4)) d.current_users = sr.read_u32();
    return d;
}

/* ── Main SMF74-3 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_3Record read_smf74_3(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_3Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr omvs_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, omvs_ptr, 100, sr))
        out.omvs = read_smf74_3_omvs(sr);

    return out;
}

} // namespace smf
