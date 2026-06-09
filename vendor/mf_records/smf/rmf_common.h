#pragma once
/*
 * smf/rmf_common.h — Shared components for RMF record types (70-79).
 *
 * RMF records share a common "Product Section" (PRO) which provides
 * interval timing and system identification.
 */

#include "../dataset_reader.h"
#include "../mf_types.h"

#include <cstdint>
#include <string>

namespace smf {

/* ── RMF Product Section (PRO) ─────────────────────────────────────────── */

struct RmfProduct {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

[[nodiscard]] inline RmfProduct read_rmf_product(mf::Reader& sr) {
    RmfProduct p;
    // Standard RMF Product Section (PRO) layout.
    // Offset 0: version (1)
    // Offset 1: product (8)
    // Offset 9: interval start time (4)
    // Offset 13: interval start date (4)
    // Offset 17: interval duration (4)
    // Offset 21: sample count (2)
    // Offset 23: flags (1)
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // version
    sr.skip(8);                                // product name
    p.interval_start_time = sr.read_mf_time(); // IST
    p.interval_start_date = sr.read_mf_date(); // DAT
    p.interval_hund       = sr.read_u32();     // INT
    p.sample_count        = sr.read_u16();     // SAM
    sr.skip(1);                                // FLA
    
    // Extensions start at offset 24
    if (sr.can_read(4))  sr.skip(4);           // CYC
    if (sr.can_read(8))  sr.skip(8);           // MVS level
    if (sr.can_read(4))  sr.skip(4);           // SRL etc.
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // XNM (offset 40)
    return p;
}

} // namespace smf
