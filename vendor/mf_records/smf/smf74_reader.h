#pragma once
/*
 * smf/smf74_reader.h — SMF Type 74 Subtype 1: RMF DASD Device Activity (parse core).
 */

#include "rmf_common.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

struct Smf74Device {
    std::string device_num;       // SMF74NUM
    std::string volume_serial;    // SMF74SER
    std::string storage_group;    // SMF74SGN
    uint8_t     device_flags{0};  // SMF74CNF
    uint32_t    ssch_count{0};    // SMF74SSC
    uint32_t    connect_hund{0};  // SMF74CNN
    uint32_t    pending_hund{0};  // SMF74PEN
    uint32_t    active_hund{0};   // SMF74ATV
    uint32_t    disconnect_hund{0}; // SMF74DIS
    uint32_t    queue_depth{0};   // SMF74QUE
};

struct Smf74Record {
    mf::SmfHeader   header;
    uint16_t        subtype{0};
    RmfProduct      product;
    std::vector<Smf74Device> devices;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74Device read_smf74_device(mf::Reader& er) {
    Smf74Device d;
    if (er.can_read(2)) d.device_num = mf::rtrim(er.read_ebcdic(2));
    if (er.can_read(2)) er.skip(2);
    if (er.can_read(1)) d.device_flags = er.read_u8();
    if (er.can_read(6)) d.volume_serial = mf::rtrim(er.read_ebcdic(6));
    
    if (er.can_read(16 + 4)) {
        er.pos = 16;
        d.ssch_count = er.read_u32();
    }
    if (er.can_read(24 + 16)) {
        er.pos = 24;
        d.connect_hund    = er.read_u32();
        d.pending_hund    = er.read_u32();
        d.active_hund     = er.read_u32();
        d.disconnect_hund = er.read_u32();
    }
    if (er.can_read(40 + 4)) {
        er.pos = 40;
        d.queue_depth     = er.read_u32();
    }
    if (er.can_read(64 + 8)) {
        er.pos = 64;
        d.storage_group   = mf::rtrim(er.read_ebcdic(8));
    }
    return d;
}

/* ── Main SMF74 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf74Record read_smf74(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // TRN at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r);
    const SectionPtr ctrl_ptr = read_section_ptr(r);
    const SectionPtr dev_ptr  = read_section_ptr(r);

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_rmf_product(sr);

    const uint32_t actual_count = safe_count(dev_ptr, rec_bytes.size());
    if (actual_count > 0 && dev_ptr.len >= 29) {
        out.devices.reserve(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            const std::size_t off = dev_ptr.offset + static_cast<std::size_t>(i) * dev_ptr.len;
            if (off + dev_ptr.len > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, dev_ptr.len) };
            out.devices.push_back(read_smf74_device(er));
        }
    }

    return out;
}

} // namespace smf
