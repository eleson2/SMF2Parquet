#pragma once
/*
 * smf/smf74_3_reader.h — SMF Type 74 Subtype 3: RMF OMVS Activity (parse core).
 *
 * One record per RMF measurement interval per system.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S3/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_3Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_3OmvsData {
    uint32_t syscall_count{0};    // R743SYSC (Float 4 in mapping, we'll read as u32 or handle float)
    uint32_t syscall_cpu_ms{0};   // R743CPU
    uint32_t max_processes{0};    // R743MAXP
    uint32_t max_users{0};        // R743MAXU
    uint32_t current_processes{0};// R743CURP
    uint32_t current_users{0};    // R743CURU
};

struct Smf74_3Record {
    mf::SmfHeader  header;
    uint16_t       subtype{0};
    Smf74_3Product product;
    Smf74_3OmvsData omvs;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_3Product read_smf74_3_product(mf::Reader& sr) {
    Smf74_3Product p;
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

[[nodiscard]] inline Smf74_3OmvsData read_smf74_3_omvs(mf::Reader& sr) {
    Smf74_3OmvsData d;
    if (sr.can_read(12)) sr.skip(12); // CYCU, CYCT, FLG, RSV
    if (sr.can_read(4)) d.syscall_count = sr.read_u32(); // R743SYSC
    sr.skip(8); // SCMN, SCMX
    if (sr.can_read(4)) d.syscall_cpu_ms = sr.read_u32(); // R743CPU
    sr.skip(8); // CTMN, CTMX
    sr.skip(24); // Fork/Dub failure counts
    if (sr.can_read(4)) d.max_processes = sr.read_u32(); // R743MAXP
    if (sr.can_read(4)) d.max_users = sr.read_u32(); // R743MAXU
    sr.skip(4); // MXPU
    if (sr.can_read(4)) d.current_processes = sr.read_u32(); // R743CURP
    sr.skip(8); // CPMN, CPMX
    if (sr.can_read(4)) d.current_users = sr.read_u32(); // R743CURU
    return d;
}

/* ── Main SMF74-3 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_3Record read_smf74_3(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_3Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out;

    r.pos = 26;
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr omvs_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_3_product(sr);

    if (make_section_reader(rec_bytes, omvs_ptr, 100, sr))
        out.omvs = read_smf74_3_omvs(sr);

    return out;
}

} // namespace smf
