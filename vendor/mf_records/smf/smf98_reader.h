#pragma once
/*
 * smf/smf98_reader.h — SMF Type 98 Subtype 1: z/OS Supervisor Activity (parse core).
 *
 * High-frequency performance data (5-second intervals).
 * Record structure uses 8-byte triplets (4-byte offset, 2-byte length, 2-byte count).
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 24     SMF98IND: Flags
 *   Offset 26     SMF98SDSLEN: Length of self-defining section
 *   Offset 28     SMF98SDSTRIPLETSNUM: Number of triplets
 *   Offset 32+    Triplet area (8 bytes per triplet)
 *
 * Triplet order:
 *   [0] Identification
 *   [1] Context Summary
 *   [2] Environment
 *   ...
 *   [5] Utilization
 *   ...
 *   [11] Consumption (Address Space)
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Triplet8 {
    uint32_t offset{0};
    uint16_t length{0};
    uint16_t count{0};
};

[[nodiscard]] inline Triplet8 read_triplet_8(mf::Reader& r) {
    if (!r.can_read(8)) return Triplet8{};
    Triplet8 t;
    t.offset = r.read_u32();
    t.length = r.read_u16();
    t.count  = r.read_u16();
    return t;
}

struct Smf98Utilization {
    uint64_t cpu_busy_time{0}; // offset varies, common metric
};

struct Smf98Consumption {
    uint16_t    asid{0};
    std::string job_name;
    uint64_t    cpu_time_us{0};
};

struct Smf98Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    std::vector<Smf98Consumption> as_consumption;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf98Consumption read_smf98_consume(mf::Reader& sr) {
    Smf98Consumption c;
    if (sr.can_read(2)) c.asid = sr.read_u16();
    if (sr.can_read(2 + 8)) {
        sr.pos = 2;
        c.job_name = mf::rtrim(sr.read_ebcdic(8));
    }
    // CPU time is deeper in nested triplets, skipping for now
    return c;
}

/* ── Main SMF98 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf98Record read_smf98(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf98Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 28;
    uint16_t num_triplets = r.can_read(2) ? r.read_u16() : 0;
    
    r.pos = 32;
    if (num_triplets < 12) return out;

    // Skip first 11 triplets to get to Consumption Section [11]
    for (int i = 0; i < 11; ++i) read_triplet_8(r);
    const Triplet8 cons_ptr = read_triplet_8(r);

    if (cons_ptr.count > 0 && cons_ptr.length >= 10) {
        out.as_consumption.reserve(cons_ptr.count);
        for (uint16_t i = 0; i < cons_ptr.count; ++i) {
            const std::size_t off = cons_ptr.offset + static_cast<std::size_t>(i) * cons_ptr.length;
            if (off + cons_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, cons_ptr.length) };
            out.as_consumption.push_back(read_smf98_consume(er));
        }
    }

    return out;
}

} // namespace smf
