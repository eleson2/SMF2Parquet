#pragma once
/*
 * smf/smf77_reader.h — SMF Type 77 record parser (parse core): RMF coupling facility activity.
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf77EnqueueData {
    std::string major_name;      // SMF77QNM
    std::string minor_name;      // SMF77RNM
    uint32_t    total_wait_ms{0}; // SMF77WTT
    uint32_t    max_wait_ms{0};   // SMF77WTX
    uint32_t    event_count{0};   // SMF77EVT
};

struct Smf77Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    RmfProduct               product;
    std::vector<Smf77EnqueueData> enqueues;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf77EnqueueData read_smf77_enqueue(mf::Reader& er) {
    Smf77EnqueueData d;
    if (er.can_read(8))  d.major_name = mf::rtrim(er.read_ebcdic(8));
    if (er.can_read(44)) d.minor_name = mf::rtrim(er.read_ebcdic(44));
    er.skip(4);
    if (er.can_read(4)) d.max_wait_ms   = (er.read_u32() * 1024) / 1000;
    if (er.can_read(4)) d.total_wait_ms = (er.read_u32() * 1024) / 1000;
    return d;
}

/* ── Main SMF77 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf77Record read_smf77(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf77Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr enq_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(enq_ptr, rec_bytes.size());
    if (actual_count > 0 && enq_ptr.len >= 60) {
        out.enqueues.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = enq_ptr.offset + static_cast<std::size_t>(i) * enq_ptr.len;
            if (off + enq_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, enq_ptr.len) };
            out.enqueues.push_back(read_smf77_enqueue(er));
        }
    }

    return out;
}

} // namespace smf
