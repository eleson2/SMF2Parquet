#pragma once
/*
 * smf/smf72_reader.h — SMF Type 72 record parser (parse core): RMF Workload Activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf72Control {
    std::string policy_name;
    std::string workload_name;
    std::string class_name;
};

struct Smf72Period {
    uint32_t    period_num{0};    // R723CPER
    uint32_t    importance{0};    // R723CIMP
    uint64_t    service_units{0}; // R723CSRV
    uint64_t    cpu_units{0};     // R723CCPU
    uint64_t    ioc_units{0};     // R723CIOC
    uint64_t    mso_units{0};     // R723CMSO
    uint64_t    srb_units{0};     // R723CSRB
    uint32_t    trans_ended{0};   // R723CPRO
    uint64_t    trans_elapsed_hund{0}; // R723CTET
};

struct Smf72Record {
    mf::SmfHeader  header;
    uint16_t       subtype{0};
    RmfProduct     product;
    Smf72Control   control;
    std::vector<Smf72Period> periods;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf72Control read_smf72_control(mf::Reader& sr) {
    Smf72Control c;
    if (sr.can_read(8)) c.policy_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8)) c.workload_name = mf::rtrim(sr.read_ebcdic(8));
    if (sr.can_read(8)) c.class_name = mf::rtrim(sr.read_ebcdic(8));
    return c;
}

[[nodiscard]] inline Smf72Period read_smf72_period(mf::Reader& er) {
    Smf72Period p;
    if (er.can_read(4)) p.period_num = er.read_u32();
    if (er.can_read(4)) p.importance = er.read_u32();
    if (er.can_read(8)) p.service_units = er.read_u64();
    if (er.can_read(8)) p.cpu_units = er.read_u64();
    if (er.can_read(8)) p.ioc_units = er.read_u64();
    if (er.can_read(8)) p.mso_units = er.read_u64();
    if (er.can_read(8)) p.srb_units = er.read_u64();
    er.skip(8);
    if (er.can_read(4)) p.trans_ended = er.read_u32();
    if (er.can_read(8)) p.trans_elapsed_hund = er.read_u64();
    return p;
}

/* ── Main SMF72 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf72Record read_smf72(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf72Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    r.skip(12 * 2);
    const SectionPtr peri_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    if (make_section_reader(rec_bytes, ctrl_ptr, 24, sr))
        out.control = read_smf72_control(sr);

    const uint32_t actual_count = safe_count(peri_ptr, rec_bytes.size());
    if (actual_count > 0 && peri_ptr.len >= 68) {
        out.periods.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = peri_ptr.offset + static_cast<std::size_t>(i) * peri_ptr.len;
            if (off + peri_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, peri_ptr.len) };
            out.periods.push_back(read_smf72_period(er));
        }
    }

    return out;
}

} // namespace smf
