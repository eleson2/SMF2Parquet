#pragma once
/*
 * smf/smf74_2_reader.h — SMF Type 74 Subtype 2: RMF XCF Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_2Path {
    std::string system_name;    // R742PNME
    uint16_t    device_num{0};   // R742PDEV
    uint8_t     direction{0};    // R742PDIR
    uint32_t    signals_sent{0}; // R742PSIG
    std::string other_system;   // R742PONA
};

struct Smf74_2Member {
    std::string system_name;    // R742MSYS
    std::string group_name;     // R742MGRP
    std::string member_name;    // R742MMEM
    uint32_t    signals_sent{0}; // R742MSNT
    uint32_t    signals_rcvd{0}; // R742MRCV
};

struct Smf74_2Record {
    mf::SmfHeader              header;
    uint16_t                   subtype{0};
    RmfProduct                 product;
    std::vector<Smf74_2Path>   paths;
    std::vector<Smf74_2Member> members;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_2Path read_smf74_2_path(mf::Reader& sr) {
    Smf74_2Path p;
    if (sr.can_read(8)) p.system_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(2)) p.device_num = sr.read_u16();
    sr.skip(3);
    if (sr.can_read(1)) p.direction = sr.read_u8();
    sr.skip(1);
    if (sr.can_read(8)) p.other_system = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(38 - sr.pos)) sr.skip(38 - sr.pos);
    if (sr.can_read(4)) p.signals_sent = sr.read_u32();
    return p;
}

[[nodiscard]] inline Smf74_2Member read_smf74_2_member(mf::Reader& sr) {
    Smf74_2Member m;
    if (sr.can_read(8))  m.system_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8))  m.group_name  = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(16)) m.member_name = mf::rtrim(sr.read_ebcdic(16));
    if (sr.can_read(35 - sr.pos)) sr.skip(35 - sr.pos);
    if (sr.can_read(4)) m.signals_sent = sr.read_u32();
    if (sr.can_read(4)) m.signals_rcvd = sr.read_u32();
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
    const SectionPtr prod_ptr   = read_section_ptr(r);
    const SectionPtr ctrl_ptr   = read_section_ptr(r);
    const SectionPtr sys_ptr    = read_section_ptr(r);
    const SectionPtr path_ptr   = read_section_ptr(r);
    const SectionPtr member_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_path_count = safe_count(path_ptr, rec_bytes.size());
    if (actual_path_count > 0 && path_ptr.len >= 42) {
        out.paths.reserve(actual_path_count);
        for (uint32_t i = 0; i < actual_path_count; ++i) {
            const std::size_t off = path_ptr.offset + static_cast<std::size_t>(i) * path_ptr.len;
            if (off + path_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, path_ptr.len) };
            out.paths.push_back(read_smf74_2_path(er));
        }
    }

    const uint32_t actual_member_count = safe_count(member_ptr, rec_bytes.size());
    if (actual_member_count > 0 && member_ptr.len >= 43) {
        out.members.reserve(actual_member_count);
        for (uint32_t i = 0; i < actual_member_count; ++i) {
            const std::size_t off = member_ptr.offset + static_cast<std::size_t>(i) * member_ptr.len;
            if (off + member_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, member_ptr.len) };
            out.members.push_back(read_smf74_2_member(er));
        }
    }

    return out;
}

} // namespace smf
