#pragma once
/*
 * smf/smf1154_reader.h — SMF Type 1154 Compliance Evidence (parse core).
 *
 * z/OS 2.5+ modern compliance evidence records.
 * Record structure uses a "common header" + subtype-specific data.
 *
 * IBM reference:
 *   https://www.ibm.com/docs/en/zos/3.1.0?topic=records-type-1154-compliance-evidence
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf1154CommonHeader {
    std::string system_name;     // R1154SYS (8 bytes)
    std::string sysplex_name;    // R1154PLEX (8 bytes)
    std::string job_name;        // R1154JOB (8 bytes)
    std::string request_id;      // R1154REQID (24 bytes)
};

// Subtype 1: TCP/IP Stack Info
struct Smf1154_1TcpStack {
    std::string stack_name;      // SMF1154_1_STKName (8 bytes)
    uint8_t     ip_version;      // SMF1154_1_STKIPVer (1 byte)
    uint32_t    up_time_sec;     // SMF1154_1_STKUpTime (4 bytes)
};

struct Smf1154Record {
    mf::SmfHeader          header;
    uint16_t               subtype{0};
    Smf1154CommonHeader    common;
    std::vector<Smf1154_1TcpStack> tcp_stacks;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf1154CommonHeader read_smf1154_common(mf::Reader& sr) {
    Smf1154CommonHeader c;
    if (sr.can_read(8))  c.system_name  = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8))  c.sysplex_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8))  c.job_name     = mf::rtrim(sr.read_ebcdic(8));
    sr.skip(8); // R1154VER + Reserved
    if (sr.can_read(24)) c.request_id   = mf::rtrim(sr.read_ebcdic(24));
    return c;
}

[[nodiscard]] inline Smf1154_1TcpStack read_smf1154_1_tcp(mf::Reader& sr) {
    Smf1154_1TcpStack s;
    if (sr.can_read(8)) s.stack_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(1)) s.ip_version = sr.read_u8();
    sr.skip(3); // Reserved
    if (sr.can_read(4)) s.up_time_sec = sr.read_u32();
    return s;
}

/* ── Main SMF1154 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf1154Record read_smf1154(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf1154Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    // SMF 1154 is an Extended Header record (32 bytes).
    // Offset 22: Subtype (2 bytes) -- read above
    // Offset 24: SMF_TRN (4 bytes)
    // Offset 28: SMF_SSI (4 bytes)
    // Offset 32: Triplets start
    
    r.pos = 32;
    // [0] Common Header
    // [1] Subtype Specific Section (Self-Defining)
    const SectionPtr comm_ptr = read_section_ptr(r);
    const SectionPtr spec_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};
    if (make_section_reader(rec_bytes, comm_ptr, 64, sr))
        out.common = read_smf1154_common(sr);

    if (out.subtype == 1 && spec_ptr.count > 0) {
        // For Subtype 1, the specific section contains its own triplets
        mf::Reader spec_r{ rec_bytes.subspan(spec_ptr.offset, spec_ptr.length) };
        if (spec_r.can_read(4)) {
            uint32_t spec_trn = spec_r.read_u32(); // Offset 0 of spec section is its TRN
            if (spec_trn > 0) {
                // Triplets in spec section follow the 4-byte TRN
                const SectionPtr tcp_ptr = read_section_ptr(spec_r);
                
                if (tcp_ptr.length >= 16) {
                    const uint32_t actual_count = tcp_ptr.safe_count(rec_bytes.size());
                    if (actual_count > 0) {
                        out.tcp_stacks.reserve(actual_count);
                        for (uint32_t i = 0; i < actual_count; ++i) {
                            const std::size_t off = tcp_ptr.offset + static_cast<std::size_t>(i) * tcp_ptr.length;
                            mf::Reader er{ rec_bytes.subspan(off, tcp_ptr.length) };
                            out.tcp_stacks.push_back(read_smf1154_1_tcp(er));
                        }
                    }
                }
            }
        }
    }

    return out;
}

} // namespace smf
