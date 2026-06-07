#pragma once
/*
 * smf/smf78_reader.h — SMF Type 78 record parser (parse core).
 *
 * Type 78 records describe various performance metrics.
 * Subtype 3: I/O Queuing Activity (LCU activity).
 *
 * Record structure (z/OS 3.1, IBM SA23-1370 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 3 for I/O Queuing)
 *   Offset 22+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard 4-4-4 order)
 *
 * Triplets for Subtype 3 (starting at offset 28 in z/OS 3.1 as per search):
 *   [0] Product section      (SMF78PRS / SMF78PRL / SMF78PRN)
 *   [1] Config Control       (SMF78DCS)
 *   [2] I/O Queuing Data     (SMF78ASS / SMF78ASL / SMF78ASN) - LCU data
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf78/SMF78S3/
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf78 {
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

    // LCU Data
    LCU_ID        = 200,
    LCU_CSS       = 201,
    QUEUE_LEN_SUM = 202,
    QUEUE_LEN_COUNT = 203,
};
} // namespace smf78

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf78Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf78_3LcuData {
    uint16_t    lcu_id{0};          // R783ID2
    uint8_t     css_id{0};          // R783CSS
    uint32_t    queue_len_sum{0};   // R783QSM
    uint32_t    queue_len_count{0}; // R783QCT
};

struct Smf78Record {
    mf::SmfHeader          header;
    uint16_t               subtype{0};
    Smf78Product           product;
    std::vector<Smf78_3LcuData> lcus;

    [[nodiscard]] mf::FieldValue get_field(smf78::FieldID fid) const {
        using namespace smf78;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf78Product read_smf78_product(mf::Reader& sr) {
    Smf78Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // SMF78MFV
    sr.skip(8);                                // SMF78PRD
    p.interval_start_time = sr.read_mf_time(); // SMF78IST
    p.interval_start_date = sr.read_mf_date(); // SMF78DAT
    p.interval_hund       = sr.read_u32();     // SMF78INT
    p.sample_count        = sr.read_u16();     // SMF78SAM
    sr.skip(1);                                // SMF78FLA
    if (sr.can_read(4)) sr.skip(4);            // SMF78CYC
    if (sr.can_read(8)) sr.skip(8);            // SMF78MVS
    if (sr.can_read(4)) sr.skip(4);            // IML/PRF/PTN/SRL
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF78XNM
    return p;
}

[[nodiscard]] inline Smf78_3LcuData read_smf78_3_lcu(mf::Reader& er) {
    Smf78_3LcuData d;
    if (er.can_read(2)) d.lcu_id = er.read_u16(); // R783ID2
    if (er.can_read(1)) d.css_id = er.read_u8();  // R783CSS
    er.skip(13); // skip to queue metrics at offset 16
    if (er.can_read(4)) d.queue_len_sum = er.read_u32();   // R783QSM
    if (er.can_read(4)) d.queue_len_count = er.read_u32(); // R783QCT
    return d;
}

/* ── Main SMF78 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf78Record read_smf78(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf78Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out; // Only subtype 3 for now

    // Triplet area starts at offset 28 for type 78-3 (as per search)
    r.pos = 28;
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr conf_ptr = read_section_ptr(r); // [1] Config Control
    const SectionPtr lcu_ptr  = read_section_ptr(r); // [2] I/O Queuing Data

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf78_product(sr);

    if (lcu_ptr.count > 0 && lcu_ptr.length >= 24) {
        out.lcus.reserve(lcu_ptr.count);
        for (uint32_t i = 0; i < lcu_ptr.count; ++i) {
            const std::size_t off = lcu_ptr.offset + static_cast<std::size_t>(i) * lcu_ptr.length;
            if (off + lcu_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, lcu_ptr.length) };
            out.lcus.push_back(read_smf78_3_lcu(er));
        }
    }

    return out;
}

} // namespace smf
