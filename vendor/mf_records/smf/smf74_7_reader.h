#pragma once
/*
 * smf/smf74_7_reader.h — SMF Type 74 Subtype 7: RMF FICON Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains multiple
 * port entries.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S7/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74_7Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf74_7PortData {
    uint16_t port_num{0};     // R747PNUM
    uint16_t port_addr{0};    // R747PADR
    double   words_rcvd{0};   // R747PNWR (IBM Float 8)
    double   words_sent{0};   // R747PNWT
    double   frames_rcvd{0};  // R747PNFR
    double   frames_sent{0};  // R747PNFT
};

struct Smf74_7Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    Smf74_7Product           product;
    std::vector<Smf74_7PortData> ports;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74_7Product read_smf74_7_product(mf::Reader& sr) {
    Smf74_7Product p;
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

[[nodiscard]] inline Smf74_7PortData read_smf74_7_port(mf::Reader& sr) {
    Smf74_7PortData d;
    if (sr.can_read(2)) d.port_num = sr.read_u16();
    if (sr.can_read(2)) d.port_addr = sr.read_u16();
    sr.skip(24); // flags, IDs, counts, index, frame pacing time
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

    r.pos = 28; // Triplets start at 28 for 74-7 (as per search)
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    r.skip(12 * 2);                                  // [1] Global, [2] Switch
    const SectionPtr port_ptr = read_section_ptr(r); // [3] Port

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_7_product(sr);

    if (port_ptr.count > 0 && port_ptr.length >= 68) {
        out.ports.reserve(port_ptr.count);
        for (uint32_t i = 0; i < port_ptr.count; ++i) {
            const std::size_t off = port_ptr.offset + static_cast<std::size_t>(i) * port_ptr.length;
            if (off + port_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, port_ptr.length) };
            out.ports.push_back(read_smf74_7_port(er));
        }
    }

    return out;
}

} // namespace smf
