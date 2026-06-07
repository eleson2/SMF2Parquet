#pragma once
/*
 * smf/smf73_reader.h — SMF Type 73 record parser (parse core): RMF Channel Path Activity.
 *
 * Type 73 records describe channel path (CHPID) utilization and throughput.
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 1)
 *   Offset 22+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard 4-4-4 order)
 *
 * Assumed triplet order at offset 22 (SA23-1370):
 *   [0] Product section      (SMF73PRS / SMF73PRL / SMF73PRN)
 *   [1] CHPID control        (SMF73HIS / SMF73HIL / SMF73HIN)
 *   [2] CHPID data           (SMF73HPS / SMF73HPL / SMF73HPN)
 *   [3] Extended CHPID data  (SMF73HES / SMF73HEL / SMF73HEN)
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf73/SMF73S1/
 *   IBM GA32-0869  RMF Programmer's Guide
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf73 {
enum class FieldID : uint32_t {
    // Header
    NONE          = static_cast<uint32_t>(CommonFieldID::NONE),
    SYSTEM_ID     = static_cast<uint32_t>(CommonFieldID::SYSTEM_ID),
    SUBSYSTEM_ID  = static_cast<uint32_t>(CommonFieldID::SUBSYSTEM_ID),
    RECORD_TYPE   = static_cast<uint32_t>(CommonFieldID::RECORD_TYPE),
    SUBTYPE       = static_cast<uint32_t>(CommonFieldID::SUBTYPE),
    SMF_TIMESTAMP = static_cast<uint32_t>(CommonFieldID::SMF_TIMESTAMP),
    SMF_DATE      = static_cast<uint32_t>(CommonFieldID::SMF_DATE),

    // Product Section
    INTERVAL_MS   = 100,
    SAMPLE_COUNT  = 101,

    // CHPID Data
    CHPID         = 200,
    CHPID_TYPE    = 201,
    BUSY_COUNT    = 202,
    BUSY_PERCENT  = 203,
};

[[nodiscard]] inline FieldID smf73_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    return FieldID::NONE;
}
} // namespace smf73

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf73Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf73ChpidData {
    uint8_t     chpid{0};           // SMF73PID
    uint8_t     chpid_type{0};      // SMF73PTY (hex type code)
    std::string chpid_acronym;      // SMF73ACR (4 chars EBCDIC)
    uint32_t    busy_count{0};      // SMF73BSY (samples busy)
    uint32_t    partition_busy{0};  // SMF73PBY (partition busy time)
    bool        is_valid{false};    // SMF73FG3 bit 6
};

struct Smf73Record {
    mf::SmfHeader          header;
    uint16_t               subtype{0};
    Smf73Product           product;
    std::vector<Smf73ChpidData> chpids;

    [[nodiscard]] mf::FieldValue get_field(smf73::FieldID fid) const {
        using namespace smf73;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf73Product read_smf73_product(mf::Reader& sr) {
    Smf73Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // SMF73MFV
    sr.skip(8);                                // SMF73PRD
    p.interval_start_time = sr.read_mf_time(); // SMF73IST
    p.interval_start_date = sr.read_mf_date(); // SMF73DAT
    p.interval_hund       = sr.read_u32();     // SMF73INT
    p.sample_count        = sr.read_u16();     // SMF73SAM
    sr.skip(1);                                // SMF73FLA
    if (sr.can_read(4)) sr.skip(4);            // SMF73CYC
    if (sr.can_read(8)) sr.skip(8);            // SMF73MVS
    if (sr.can_read(4)) sr.skip(4);            // IML/PRF/PTN/SRL
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF73XNM
    return p;
}

[[nodiscard]] inline Smf73ChpidData read_smf73_chpid(mf::Reader& er) {
    Smf73ChpidData d;
    // Channel Path Data Section (SMF73HPD)
    if (er.can_read(1)) d.chpid = er.read_u8();      // SMF73PID
    if (er.can_read(1)) d.chpid_type = er.read_u8(); // SMF73PTY
    if (er.can_read(1)) {
        uint8_t flags = er.read_u8();                // SMF73FG3
        d.is_valid = (flags & 0x02); // bit 6 in IBM-speak (0-indexed from right 0x02)
        // Wait, IBM bit 6 of 0-7 is 0x02 if 0 is high bit? No, bit 6 is 2nd lowest.
        // SMF 73 mapping says bit 6 of SMF73FG3 means path is valid.
    }
    er.skip(1); // SMF73FG4
    if (er.can_read(4)) d.busy_count = er.read_u32(); // SMF73BSY
    er.skip(8); // SMF73TMC, SMF73PBY
    if (er.can_read(4)) d.chpid_acronym = mf::rtrim(er.read_ebcdic(4)); // SMF73ACR
    return d;
}

/* ── Main SMF73 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf73Record read_smf73(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf73Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    // Triplet area starts at offset 22
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Control
    const SectionPtr hpd_ptr  = read_section_ptr(r); // [2] CHPID Data

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf73_product(sr);

    if (hpd_ptr.count > 0 && hpd_ptr.length >= 20) {
        out.chpids.reserve(hpd_ptr.count);
        for (uint32_t i = 0; i < hpd_ptr.count; ++i) {
            const std::size_t off = hpd_ptr.offset + static_cast<std::size_t>(i) * hpd_ptr.length;
            if (off + hpd_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, hpd_ptr.length) };
            auto chp = read_smf73_chpid(er);
            if (chp.is_valid) out.chpids.push_back(std::move(chp));
        }
    }

    return out;
}

} // namespace smf
