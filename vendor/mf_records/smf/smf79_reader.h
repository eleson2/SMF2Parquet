#pragma once
/*
 * smf/smf79_reader.h — SMF Type 79 record parser (parse core): RMF Monitor II Activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

// Subtype 1: ASD
struct Smf79_1AsState {
    uint16_t    asid{0};         // r791asid
    std::string job_name;        // r791jbn
    uint32_t    total_cpu_ms{0}; // r791tcpu
    uint32_t    real_frames{0};  // r791fmct
    std::string service_class;   // r791scl
    uint32_t    ziip_ms{0};      // r791tsup
};

// Subtype 2: ARD
struct Smf79_2AsResource {
    uint16_t    asid{0};         // r792asid
    std::string job_name;        // r792jbn
    uint32_t    total_cpu_ms{0}; // r792tcpu
    uint32_t    fixed_frames{0}; // r792prfx
    std::string service_class;   // r792scl
};

// Subtype 13: SEV
struct Smf79_13Event {
    uint16_t    event_type{0};   // R79DEVT
    std::string event_data;      // R79DEVD
};

struct Smf79Record {
    mf::SmfHeader                 header;
    uint16_t                      subtype{0};
    RmfProduct                    product;
    std::vector<Smf79_1AsState>    as_states;     // Subtype 1
    std::vector<Smf79_2AsResource> as_resources;  // Subtype 2
    std::vector<Smf79_13Event>     events;        // Subtype 13
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf79_1AsState read_smf79_1_asid(mf::Reader& sr) {
    Smf79_1AsState s;
    if (sr.can_read(2)) s.asid = sr.read_u16();
    if (sr.can_read(8)) s.job_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(32 + 4)) {
        sr.pos = 32;
        s.total_cpu_ms = sr.read_u32();
    }
    if (sr.can_read(50 + 4)) {
        sr.pos = 50;
        s.real_frames = sr.read_u32();
    }
    if (sr.can_read(98 + 8)) {
        sr.pos = 98;
        s.service_class = mf::rtrim(sr.read_ebcdic(8));
    }
    if (sr.can_read(183 + 4)) {
        sr.pos = 183;
        s.ziip_ms = sr.read_u32();
    }
    return s;
}

[[nodiscard]] inline Smf79_2AsResource read_smf79_2_asid(mf::Reader& sr) {
    Smf79_2AsResource s;
    if (sr.can_read(2)) s.asid = sr.read_u16();
    if (sr.can_read(8)) s.job_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(32 + 4)) {
        sr.pos = 32;
        s.total_cpu_ms = sr.read_u32();
    }
    if (sr.can_read(86 + 4)) {
        sr.pos = 86;
        s.fixed_frames = sr.read_u32();
    }
    if (sr.can_read(116 + 8)) {
        sr.pos = 116;
        s.service_class = mf::rtrim(sr.read_ebcdic(8));
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
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr data_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (out.subtype == 1) {
        const uint32_t actual_count = safe_count(data_ptr, rec_bytes.size());
        if (actual_count > 0 && data_ptr.len >= 200) {
            out.as_states.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                if (off + data_ptr.len > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                out.as_states.push_back(read_smf79_1_asid(er));
            }
        }
    } else if (out.subtype == 2) {
        const uint32_t actual_count = safe_count(data_ptr, rec_bytes.size());
        if (actual_count > 0 && data_ptr.len >= 150) {
            out.as_resources.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                if (off + data_ptr.len > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                out.as_resources.push_back(read_smf79_2_asid(er));
            }
        }
    } else if (out.subtype == 13) {
        const uint32_t actual_count = safe_count(data_ptr, rec_bytes.size());
        if (actual_count > 0 && data_ptr.len >= 10) {
            out.events.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                if (off + data_ptr.len > rec_bytes.size()) break;
                mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                out.events.push_back(read_smf79_13_event(er));
            }
        }
    }

    return out;
}

} // namespace smf
