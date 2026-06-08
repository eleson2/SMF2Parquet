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

namespace smf74 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),   // sentinel: field not found
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    // Product Section
    INTERVAL_MS   = 100,
    SAMPLE_COUNT  = 101,
    SYSPLEX_NAME  = 102,

    // Device Data Section
    DEVICE_NUM    = 200,
    VOLSER        = 201,
    STORAGE_GROUP = 202,
    DEVICE_FLAGS  = 203,
    SSCH_COUNT    = 204,
    CONNECT_MS    = 205,
    PENDING_MS    = 206,
    ACTIVE_MS     = 207,
    DISCONNECT_MS = 208,
    QUEUE_DEPTH   = 209,
};

[[nodiscard]] inline FieldID smf74_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    if (name == "SMF74INT")      return FieldID::INTERVAL_MS;
    if (name == "SMF74SAM")      return FieldID::SAMPLE_COUNT;
    if (name == "SMF74XNM")      return FieldID::SYSPLEX_NAME;

    if (name == "SMF74NUM")      return FieldID::DEVICE_NUM;
    if (name == "SMF74SER")      return FieldID::VOLSER;
    if (name == "SMF74SGN")      return FieldID::STORAGE_GROUP;
    if (name == "SMF74CNF")      return FieldID::DEVICE_FLAGS;
    if (name == "SMF74SSC")      return FieldID::SSCH_COUNT;
    if (name == "SMF74CNN")      return FieldID::CONNECT_MS;
    if (name == "SMF74PEN")      return FieldID::PENDING_MS;
    if (name == "SMF74ATV")      return FieldID::ACTIVE_MS;
    if (name == "SMF74DIS")      return FieldID::DISCONNECT_MS;
    if (name == "SMF74QUE")      return FieldID::QUEUE_DEPTH;

    return FieldID::NONE;
}
} // namespace smf74

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

    [[nodiscard]] mf::FieldValue get_field(smf74::FieldID fid) const {
        using namespace smf74;
        switch (fid) {
            case FieldID::DEVICE_NUM:    return mf::FieldValue::from_string(device_num);
            case FieldID::VOLSER:        return mf::FieldValue::from_string(volume_serial);
            case FieldID::STORAGE_GROUP: return mf::FieldValue::from_string(storage_group);
            case FieldID::DEVICE_FLAGS:  return mf::FieldValue::from_int64(device_flags);
            case FieldID::SSCH_COUNT:    return mf::FieldValue::from_int64(ssch_count);
            case FieldID::CONNECT_MS:    return mf::FieldValue::from_double(connect_hund    * 10.0);
            case FieldID::PENDING_MS:    return mf::FieldValue::from_double(pending_hund    * 10.0);
            case FieldID::ACTIVE_MS:     return mf::FieldValue::from_double(active_hund     * 10.0);
            case FieldID::DISCONNECT_MS: return mf::FieldValue::from_double(disconnect_hund * 10.0);
            case FieldID::QUEUE_DEPTH:   return mf::FieldValue::from_int64(queue_depth);
            default:                     return mf::FieldValue::null();
        }
    }
};

struct Smf74Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    Smf74Product             product;
    std::vector<Smf74Device> devices;

    [[nodiscard]] mf::FieldValue get_field(smf74::FieldID fid) const {
        using namespace smf74;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            case FieldID::SAMPLE_COUNT:  return mf::FieldValue::from_int64(product.sample_count);
            case FieldID::SYSPLEX_NAME:  return mf::FieldValue::from_string(product.sysplex_name);
            
            default:                     return mf::FieldValue::null();
        }
    }
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
    // Device Data Section entry (SMF74B) verified offsets for z/OS 3.1.
    if (er.can_read(2)) d.device_num = mf::rtrim(er.read_ebcdic(2));   // SMF74NUM
    if (er.can_read(2)) er.skip(2);                                  // SMF74LCU
    if (er.can_read(1)) d.device_flags = er.read_u8();               // SMF74CNF
    if (er.can_read(6)) d.volume_serial = mf::rtrim(er.read_ebcdic(6)); // SMF74SER
    
    // Jump to fields with known offsets in long modern records
    if (er.can_read(16 + 4)) {
        er.pos = 16;
        d.ssch_count = er.read_u32();                               // SMF74SSC
    }
    if (er.can_read(24 + 16)) {
        er.pos = 24;
        d.connect_hund    = er.read_u32();                          // SMF74CNN
        d.pending_hund    = er.read_u32();                          // SMF74PEN
        d.active_hund     = er.read_u32();                          // SMF74ATV
        d.disconnect_hund = er.read_u32();                          // SMF74DIS
    }
    if (er.can_read(40 + 4)) {
        er.pos = 40;
        d.queue_depth     = er.read_u32();                          // SMF74QUE
    }
    if (er.can_read(64 + 8)) {
        er.pos = 64;
        d.storage_group   = mf::rtrim(er.read_ebcdic(8));           // SMF74SGN
    }
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
    if (dev_ptr.length >= 29) {
        const uint32_t actual_count = dev_ptr.safe_count(rec_bytes.size());
        if (actual_count > 0) {
            out.devices.reserve(actual_count);
            for (uint32_t i = 0; i < actual_count; ++i) {
                const std::size_t off = dev_ptr.offset + static_cast<std::size_t>(i) * dev_ptr.length;
                mf::Reader er{ rec_bytes.subspan(off, dev_ptr.length) };
                out.devices.push_back(read_smf74_device(er));
            }
        }
    }

    return out;
}

} // namespace smf
