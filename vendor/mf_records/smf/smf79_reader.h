#pragma once
/*
 * smf/smf79_reader.h — SMF Type 79 record parser (parse core): RMF Monitor II Activity.
 *
 * Unified Monitor II record type. Subtypes correspond to specific reports.
 *
 * Subtypes:
 *   1  ASD (Address Space State Data)
 *   2  ARD (Address Space Resource Data)
 *   13 SEV (System Evaluation / System Event Activity)
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE)
 *   Offset 22-25  SMF79TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *
 * Triplet order:
 *   [0] Product section      (SMF79PRS)
 *   [1] Control section      (SMF79MCS)
 *   [2] Data section         (SMF79ASS) — varies by subtype
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf79/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf79Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

// Subtype 1: ASD
struct Smf79_1AsState {
    uint16_t    asid{0};         // r791asid (2 bytes)
    std::string job_name;        // r791jbn (8 bytes)
    uint32_t    total_cpu_ms{0}; // r791tcpu (4 bytes)
    uint32_t    real_frames{0};  // r791fmct (4 bytes)
    std::string service_class;   // r791scl (8 bytes)
    uint32_t    ziip_ms{0};      // r791tsup (4 bytes)
};

// Subtype 2: ARD
struct Smf79_2AsResource {
    uint16_t    asid{0};         // r792asid (2 bytes)
    std::string job_name;        // r792jbn (8 bytes)
    uint32_t    total_cpu_ms{0}; // r792tcpu (4 bytes)
    uint32_t    fixed_frames{0}; // r792prfx (4 bytes)
    std::string service_class;   // r792scl (8 bytes)
};

// Subtype 13: SEV
struct Smf79_13Event {
    uint16_t    event_type{0};   // R79DEVT
    std::string event_data;      // R79DEVD (8 bytes)
};

struct Smf79Record {
    mf::SmfHeader                 header;
    uint16_t                      subtype{0};
    Smf79Product                  product;
    std::vector<Smf79_1AsState>    as_states;     // Subtype 1
    std::vector<Smf79_2AsResource> as_resources;  // Subtype 2
    std::vector<Smf79_13Event>     events;        // Subtype 13
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf79Product read_smf79_product(mf::Reader& sr) {
    Smf79Product p;
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

[[nodiscard]] inline Smf79_1AsState read_smf79_1_asid(mf::Reader& sr) {
    Smf79_1AsState s;
    if (sr.can_read(2)) s.asid = sr.read_u16();
    if (sr.can_read(8)) s.job_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(32 + 4)) {
        sr.pos = 32;
        s.total_cpu_ms = sr.read_u32(); // r791tcpu
    }
    if (sr.can_read(50 + 4)) {
        sr.pos = 50;
        s.real_frames = sr.read_u32(); // r791fmct
    }
    if (sr.can_read(98 + 8)) {
        sr.pos = 98;
        s.service_class = mf::rtrim(sr.read_ebcdic(8)); // r791scl
    }
    if (sr.can_read(183 + 4)) {
        sr.pos = 183;
        s.ziip_ms = sr.read_u32(); // r791tsup
    }
    return s;
}

[[nodiscard]] inline Smf79_2AsResource read_smf79_2_asid(mf::Reader& sr) {
    Smf79_2AsResource s;
    if (sr.can_read(2)) s.asid = sr.read_u16();
    if (sr.can_read(8)) s.job_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(32 + 4)) {
        sr.pos = 32;
        s.total_cpu_ms = sr.read_u32(); // r792tcpu
    }
    if (sr.can_read(86 + 4)) {
        sr.pos = 86;
        s.fixed_frames = sr.read_u32(); // r792prfx
    }
    if (sr.can_read(116 + 8)) {
        sr.pos = 116;
        s.service_class = mf::rtrim(sr.read_ebcdic(8)); // r792scl
    }
    return s;
}

[[nodiscard]] inline Smf79_13Event read_smf79_13_event(mf::Reader& sr) {
    Smf79_13Event e;
    if (sr.can_read(2)) e.event_type = sr.read_u16();
    if (sr.can_read(8)) e.event_data = mf::rtrim(sr.read_ebcdic(8));
    return e;
}

/* ── Main SMF79 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf79Record read_smf79(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf79Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Control
    const SectionPtr data_ptr = read_section_ptr(r); // [2] Data Section

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf79_product(sr);

    if (out.subtype == 1) {
        if (data_ptr.count > 0 && data_ptr.length >= 200) {
            out.as_states.reserve(data_ptr.count);
            for (uint32_t i = 0; i < data_ptr.count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.length;
                if (off + data_ptr.length > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.length) };
                out.as_states.push_back(read_smf79_1_asid(er));
            }
        }
    } else if (out.subtype == 2) {
        if (data_ptr.count > 0 && data_ptr.length >= 150) {
            out.as_resources.reserve(data_ptr.count);
            for (uint32_t i = 0; i < data_ptr.count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.length;
                if (off + data_ptr.length > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.length) };
                out.as_resources.push_back(read_smf79_2_asid(er));
            }
        }
    } else if (out.subtype == 13) {
        if (data_ptr.count > 0 && data_ptr.length >= 10) {
            out.events.reserve(data_ptr.count);
            for (uint32_t i = 0; i < data_ptr.count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.length;
                if (off + data_ptr.length > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.length) };
                out.events.push_back(read_smf79_13_event(er));
            }
        }
    }

    return out;
}

} // namespace smf
