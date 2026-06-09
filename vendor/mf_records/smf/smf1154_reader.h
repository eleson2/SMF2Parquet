#pragma once
/*
 * smf/smf1154_reader.h — SMF Type 1154 Compliance Evidence (parse core).
 */

#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf1154CommonHeader {
    std::string system_name;     // R1154SYS
    std::string sysplex_name;    // R1154PLEX
    std::string job_name;        // R1154JOB
    std::string request_id;      // R1154REQID
};

// Subtype 1: TCP/IP Stack Info
struct Smf1154_1TcpStack {
    std::string stack_name;      // SMF1154_1_STKName
    uint8_t     ip_version;      // SMF1154_1_STKIPVer
    uint32_t    up_time_sec;     // SMF1154_1_STKUpTime
};

// Subtype 2: FTP Server Config
struct Smf1154_2FtpConfig {
    bool        anonymous_allowed{false}; // SMF1154_2_FDCFAnonAllowed
    uint16_t    inactivity_timeout{0};    // SMF1154_2_FDCFInactTime
    uint16_t    port_min{0};              // SMF1154_2_FDCFPortMin
    uint16_t    port_max{0};              // SMF1154_2_FDCFPortMax
    char        sec_session_reuse{' '};   // SMF1154_2_FDCFSecSessReuse
};

// Subtype 83: RACF Configuration
struct Smf1154_83RacfSummary {
    bool        is_active{false};        // RACF_ACTIVE
    uint16_t    min_password_len{0};     // SETR_PSW_MIN_LEN
    uint16_t    password_history{0};     // SETR_PSW_HIST
    uint16_t    password_interval{0};    // SETR_PSW_INT
};

struct Smf1154Record {
    mf::SmfHeader                 header;
    uint16_t                      subtype{0};
    Smf1154CommonHeader           common;
    
    // Subtype-specific data vectors
    std::vector<Smf1154_1TcpStack>    tcp_stacks;
    std::vector<Smf1154_2FtpConfig>   ftp_configs;
    std::vector<Smf1154_83RacfSummary> racf_summaries;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf1154CommonHeader read_smf1154_common(mf::Reader& sr) {
    Smf1154CommonHeader c;
    if (sr.can_read(8))  c.system_name  = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8))  c.sysplex_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8))  c.job_name     = mf::rtrim(sr.read_ebcdic(8));
    sr.skip(8);
    if (sr.can_read(24)) c.request_id   = mf::rtrim(sr.read_ebcdic(24));
    return c;
}

[[nodiscard]] inline Smf1154_1TcpStack read_smf1154_1_tcp(mf::Reader& sr) {
    Smf1154_1TcpStack s;
    if (sr.can_read(8)) s.stack_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(1)) s.ip_version = sr.read_u8();
    sr.skip(3);
    if (sr.can_read(4)) s.up_time_sec = sr.read_u32();
    return s;
}

[[nodiscard]] inline Smf1154_2FtpConfig read_smf1154_2_ftp(mf::Reader& sr) {
    Smf1154_2FtpConfig c;
    if (sr.can_read(1)) c.anonymous_allowed = (sr.read_u8() != 0);
    sr.skip(16);
    if (sr.can_read(2)) c.inactivity_timeout = sr.read_u16();
    sr.skip(4);
    if (sr.can_read(2)) c.port_min = sr.read_u16();
    if (sr.can_read(2)) c.port_max = sr.read_u16();
    sr.skip(109 - 25);
    if (sr.can_read(1)) c.sec_session_reuse = static_cast<char>(sr.read_u8());
    return c;
}

[[nodiscard]] inline Smf1154_83RacfSummary read_smf1154_83_racf(mf::Reader& sr) {
    Smf1154_83RacfSummary s;
    if (sr.can_read(1)) s.is_active = (sr.read_u8() != 0);
    sr.skip(10);
    if (sr.can_read(2)) s.min_password_len = sr.read_u16();
    if (sr.can_read(2)) s.password_history  = sr.read_u16();
    if (sr.can_read(2)) s.password_interval = sr.read_u16();
    return s;
}

/* ── Main SMF1154 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf1154Record read_smf1154(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf1154Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    r.pos = 32;
    const SectionPtr comm_ptr = read_section_ptr(r);
    const SectionPtr spec_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};
    if (make_section_reader(rec_bytes, comm_ptr, 64, sr))
        out.common = read_smf1154_common(sr);

    if (spec_ptr.count > 0 && spec_ptr.offset + spec_ptr.len <= rec_bytes.size()) {
        mf::Reader spec_r{ rec_bytes.subspan(spec_ptr.offset, spec_ptr.len) };
        if (spec_r.can_read(4)) {
            uint32_t spec_trn = spec_r.read_u32();
            if (spec_trn > 0) {
                const SectionPtr data_ptr = read_section_ptr(spec_r);
                const uint32_t actual_count = safe_count(data_ptr, rec_bytes.size());
                
                if (actual_count > 0) {
                    if (out.subtype == 1 && data_ptr.len >= 16) {
                        out.tcp_stacks.reserve(actual_count);
                        for (uint32_t i = 0; i < actual_count; ++i) {
                            const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                            if (off + data_ptr.len > rec_bytes.size()) break;
                            mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                            out.tcp_stacks.push_back(read_smf1154_1_tcp(er));
                        }
                    } else if (out.subtype == 2 && data_ptr.len >= 112) {
                        out.ftp_configs.reserve(actual_count);
                        for (uint32_t i = 0; i < actual_count; ++i) {
                            const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                            if (off + data_ptr.len > rec_bytes.size()) break;
                            mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                            er.skip(4); // 'FDCF'
                            out.ftp_configs.push_back(read_smf1154_2_ftp(er));
                        }
                    } else if (out.subtype == 83 && data_ptr.len >= 20) {
                        out.racf_summaries.reserve(actual_count);
                        for (uint32_t i = 0; i < actual_count; ++i) {
                            const std::size_t off = data_ptr.offset + static_cast<std::size_t>(i) * data_ptr.len;
                            if (off + data_ptr.len > rec_bytes.size()) break;
                            mf::Reader er{ rec_bytes.subspan(off, data_ptr.len) };
                            out.racf_summaries.push_back(read_smf1154_83_racf(er));
                        }
                    }
                }
            }
        }
    }

    return out;
}

} // namespace smf
