#pragma once
/*
 * smf/smf74_reader.h — SMF Type 74 Subtype 1: RMF DASD Device Activity (parse core).
 *
 * One record per RMF measurement interval per system. Contains one device
 * data entry (SMF74B) per DASD device monitored. Each device becomes one
 * output row — this is a multi-entry record type (see Smf74Record::devices).
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 22-25  SMF74TRN: number of triplets (uint32 BE)
 *   Offset 26+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard order)
 *
 * Triplet order at offset 26:
 *   [0] Product section      (SMF74PRS/PRL/PRN)
 *   [1] Device control       (SMF74DCS/DCL/DCN) — one entry, not parsed in detail
 *   [2] Device data          (SMF74DDS/DDL/DDN) — one entry per DASD device
 *
 * TODO: verify exact pointer-area layout and section field byte offsets against
 *       IBM GA32-0869 for z/OS 3.1. The IBM SMF Explorer does not list byte
 *       offsets; sequential field order is assumed from the reference doc.
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf74/SMF74S1/
 *   IBM GA32-0869  RMF Programmer's Guide
 *   reference_doc/smf74_s1.md
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf74Product {
    // RMF Product Section (SMF74PRO) — same layout pattern as SMF70/75.
    // TODO: verify field offsets within section against IBM GA32-0869 z/OS 3.1.
    mf::MfTime  interval_start_time{};  // SMF74IST: interval start time
    mf::MfDate  interval_start_date{};  // SMF74DAT: interval start date
    uint32_t    interval_hund{0};       // SMF74INT: duration (hundredths of seconds)
    uint16_t    sample_count{0};        // SMF74SAM: number of samples
    std::string sysplex_name;           // SMF74XNM: sysplex name (8 chars EBCDIC)
};

struct Smf74Device {
    // Device Data Section entry (SMF74B) — one per DASD device.
    // TODO: verify field byte offsets against IBM GA32-0869 z/OS 3.1.
    std::string device_num;       // SMF74NUM: device number (4 chars EBCDIC)
    std::string volume_serial;    // SMF74SER: volume serial (6 chars EBCDIC)
    std::string storage_group;    // SMF74SGN: storage group name (8 chars EBCDIC)
    uint8_t     device_flags{0};  // SMF74CNF: device flags
    uint32_t    ssch_count{0};    // SMF74SSC: start subchannel count (I/O operations)
    uint32_t    connect_hund{0};  // SMF74CNN: device connect time (hundredths)
    uint32_t    pending_hund{0};  // SMF74PEN: device pending time (hundredths)
    uint32_t    active_hund{0};   // SMF74ATV: device active time (hundredths)
    uint32_t    disconnect_hund{0}; // SMF74DIS: device disconnect time (hundredths)
    uint32_t    queue_depth{0};   // SMF74QUE: requests queued in IOS
};

struct Smf74Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    Smf74Product             product;
    std::vector<Smf74Device> devices;
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf74Product read_smf74_product(mf::Reader& sr) {
    Smf74Product p;
    // RMF Product Section layout — same pattern as SMF70/75.
    // TODO: verify all offsets against IBM GA32-0869 for z/OS 3.1.
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // offset  0: SMF74MFV version (1 byte)
    sr.skip(8);                                // offset  1: SMF74PRD product name (8 bytes EBCDIC)
    p.interval_start_time = sr.read_mf_time(); // offset  9: SMF74IST (4 bytes)
    p.interval_start_date = sr.read_mf_date(); // offset 13: SMF74DAT (4 bytes)
    p.interval_hund       = sr.read_u32();     // offset 17: SMF74INT (4 bytes hundredths)
    p.sample_count        = sr.read_u16();     // offset 21: SMF74SAM (2 bytes)
    sr.skip(1);                                // offset 23: SMF74FLA flags
    if (sr.can_read(4))  sr.skip(4);          // offset 24: SMF74CYC sampling cycle
    if (sr.can_read(8))  sr.skip(8);          // offset 28: SMF74MVS software level
    if (sr.can_read(4))  sr.skip(4);          // offset 36: IML(1) PRF(1) PTN(1) SRL(1)
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8));  // offset 40: SMF74XNM TODO: verify
    return p;
}

[[nodiscard]] inline Smf74Device read_smf74_device(mf::Reader& er) {
    Smf74Device d;
    // Device Data Section entry (SMF74B) sequential field read.
    // TODO: verify all field sizes and order against IBM GA32-0869 z/OS 3.1.
    // Field order follows IBM SMF Explorer reference doc for SMF74S1.
    if (er.can_read(2))
        d.device_num = mf::rtrim(er.read_ebcdic(2));   // SMF74NUM: 2-byte device address
    if (er.can_read(2)) er.skip(2);                     // SMF74LCU: LCU number (2 bytes)
    if (er.can_read(1))
        d.device_flags = er.read_u8();                  // SMF74CNF: device flags
    if (er.can_read(6))
        d.volume_serial = mf::rtrim(er.read_ebcdic(6)); // SMF74SER: VOLSER (6 bytes)
    if (er.can_read(4)) er.skip(4);                     // SMF74TYP: unit type (4 bytes)
    if (er.can_read(2)) er.skip(2);                     // SMF74NUX: PAV alias count (2 bytes)
    if (er.can_read(4))
        d.ssch_count = er.read_u32();                   // SMF74SSC: SSCH count
    if (er.can_read(4)) er.skip(4);                     // SMF74MEC: measurement event count
    if (er.can_read(4))
        d.connect_hund = er.read_u32();                 // SMF74CNN: connect time (hundredths)
    if (er.can_read(4))
        d.pending_hund = er.read_u32();                 // SMF74PEN: pending time (hundredths)
    if (er.can_read(4))
        d.active_hund = er.read_u32();                  // SMF74ATV: active time (hundredths)
    if (er.can_read(4))
        d.disconnect_hund = er.read_u32();              // SMF74DIS: disconnect time (hundredths)
    if (er.can_read(4))
        d.queue_depth = er.read_u32();                  // SMF74QUE: IOS queue depth
    // Remaining fields (utl, rsv, alc, mtp, nrd, cof, dvb, clf, sgn, ...) skipped for now.
    // TODO: read storage group name (SMF74SGN, 8 bytes EBCDIC) once offset confirmed.
    return d;
}

/* ── Main SMF74 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf74Record read_smf74(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf74Record out;

    out.header  = mf::read_smf_header(r);  // advances to byte 20
    if (r.can_read(2)) out.subtype = r.read_u16();  // bytes 20-21 (absent on header-only records)

    // SMF74TRN: number of triplets (4 bytes at offset 22).
    // TODO: verify presence of SMF74TRN field vs. direct triplets from byte 22.
    [[maybe_unused]] const uint32_t trn = r.can_read(4) ? r.read_u32() : 0u;

    // Section pointer triplets from byte 26 (offset / length / count, 12 bytes each).
    const SectionPtr prod_ptr = read_section_ptr(r);  // [0] Product section
    const SectionPtr ctrl_ptr = read_section_ptr(r);  // [1] Device control (SMF74DCS)
    const SectionPtr dev_ptr  = read_section_ptr(r);  // [2] Device data (SMF74DDS)
    (void)ctrl_ptr;  // device control section not parsed in detail

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf74_product(sr);

    // Device data section — one entry per DASD device; each becomes one row.
    if (dev_ptr.count > 0 && dev_ptr.length >= 29) {
        out.devices.reserve(dev_ptr.count);
        for (uint32_t i = 0; i < dev_ptr.count; ++i) {
            const std::size_t off = dev_ptr.offset + static_cast<std::size_t>(i) * dev_ptr.length;
            if (off + dev_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, dev_ptr.length) };
            out.devices.push_back(read_smf74_device(er));
        }
    }

    return out;
}

} // namespace smf
