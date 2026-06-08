#pragma once
/*
 * smf/smf74_2_reader.h — SMF Type 74 Subtype 2: RMF XCF Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains multiple
 * path and member entries.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S2/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_2Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_2Path {
    std::string system_name;    // R742PNME (8 bytes)
    uint16_t    device_num{0};   // R742PDEV (2 bytes)
    uint8_t     direction{0};    // R742PDIR (1 byte, 1=In, 2=Out)
    uint32_t    signals_sent{0}; // R742PSIG (4 bytes)
    std::string other_system;   // R742PONA (8 bytes)
};

struct Smf74_2Member {
    std::string system_name;    // R742MSYS (8 bytes)
    std::string group_name;     // R742MGRP (8 bytes)
    std::string member_name;    // R742MMEM (16 bytes)
    uint32_t    signals_sent{0}; // R742MSNT (4 bytes)
    uint32_t    signals_rcvd{0}; // R742MRCV (4 bytes)
};

struct Smf74_2Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    Smf74_2Product             product;
    std::vector<Smf74_2Path>   paths;
    std::vector<Smf74_2Member> members;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_2Product read_smf74_2_product(mf::Reader& sr) {
    Smf74_2Product p;
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

[[nodiscard]] inline Smf74_2Path read_smf74_2_path(mf::Reader& sr) {
    Smf74_2Path p;
    if (sr.can_read(8)) p.system_name = mf::rtrim(sr.read_ebcdic(8)); // R742PNME
    if (sr.can_read(2)) p.device_num = sr.read_u16(); // R742PDEV
    sr.skip(3); // R742PSTF, R742PDIR
    if (sr.can_read(1)) p.direction = sr.read_u8(); // R742PDIR
    sr.skip(1); // R742PTYP
    if (sr.can_read(8)) p.other_system = mf::rtrim(sr.read_ebcdic(8)); // R742PONA
    sr.pos = 38;
    if (sr.can_read(4)) p.signals_sent = sr.read_u32(); // R742PSIG
    return p;
}

[[nodiscard]] inline Smf74_2Member read_smf74_2_member(mf::Reader& sr) {
    Smf74_2Member m;
    if (sr.can_read(8))  m.system_name = mf::rtrim(sr.read_ebcdic(8)); // R742MSYS
    if (sr.can_read(8))  m.group_name  = mf::rtrim(sr.read_ebcdic(8)); // R742MGRP
    if (sr.can_read(16)) m.member_name = mf::rtrim(sr.read_ebcdic(16)); // R742MMEM
    sr.pos = 35;
    if (sr.can_read(4)) m.signals_sent = sr.read_u32(); // R742MSNT
    if (sr.can_read(4)) m.signals_rcvd = sr.read_u32(); // R742MRCV
    return m;
}

/* ── Main SMF74-2 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_2Record read_smf74_2(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_2Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 2) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr   = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr   = read_section_ptr(r); // [1] Control
    const SectionPtr sys_ptr    = read_section_ptr(r); // [2] System
    const SectionPtr path_ptr   = read_section_ptr(r); // [3] Path
    const SectionPtr member_ptr = read_section_ptr(r); // [4] Member

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_2_product(sr);

    if (path_ptr.length >= 42) {
        const uint32_t actual_count = path_ptr.safe_count(rec_bytes.size());
        if (actual_count > 0) {
            out.paths.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = path_ptr.offset + static_cast<std::size_t>(i) * path_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, path_ptr.length) };
                out.paths.push_back(read_smf74_2_path(er));
            }
        }
    }

    if (member_ptr.length >= 43) {
        const uint32_t actual_count = member_ptr.safe_count(rec_bytes.size());
        if (actual_count > 0) {
            out.members.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = member_ptr.offset + static_cast<std::size_t>(i) * member_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, member_ptr.length) };
                out.members.push_back(read_smf74_2_member(er));
            }
        }
    }

    return out;
}

} // namespace smf
