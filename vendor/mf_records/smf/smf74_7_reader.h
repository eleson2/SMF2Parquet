#pragma once
/*
 * smf/smf74_7_reader.h — SMF Type 74 Subtype 7: RMF FICON Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_7PortData {
    uint16_t port_num{0};     // R747PNUM
    uint16_t port_addr{0};    // R747PADR
    double   words_rcvd{0};   // R747PNWR
    double   words_sent{0};   // R747PNWT
    double   frames_rcvd{0};  // R747PNFR
    double   frames_sent{0};  // R747PNFT
};

struct Smf74_7Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    RmfProduct               product;
    std::vector<Smf74_7PortData> ports;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_7PortData read_smf74_7_port(mf::Reader& sr) {
    Smf74_7PortData d;
    if (sr.can_read(2)) d.port_num = sr.read_u16();
    if (sr.can_read(2)) d.port_addr = sr.read_u16();
    sr.skip(24);
    if (sr.can_read(8)) d.words_rcvd = sr.read_comp2();
    if (sr.can_read(8)) d.words_sent = sr.read_comp2();
    if (sr.can_read(8)) d.frames_rcvd = sr.read_comp2();
    if (sr.can_read(8)) d.frames_sent = sr.read_comp2();
    return d;
}

/* ── Main SMF74-7 record parser ─────────────────────────────────────────── */

[[nodiscard]] inline Smf74_7Record read_smf74_7(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74_7Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 7) return out;

    r.pos = 28; // Triplets start at 28
    const SectionPtr prod_ptr = read_section_ptr(r);
    r.skip(12 * 2);
    const SectionPtr port_ptr = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(port_ptr, rec_bytes.size());
    if (actual_count > 0 && port_ptr.len >= 68) {
        out.ports.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = port_ptr.offset + static_cast<std::size_t>(i) * port_ptr.len;
            if (off + port_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, port_ptr.len) };
            out.ports.push_back(read_smf74_7_port(er));
        }
    }

    return out;
}

} // namespace smf
