#pragma once
/*
 * smf/smf77_reader.h — SMF Type 77 record parser (parse core): RMF coupling facility activity.
 *
 * Type 77 records describe Parallel Sysplex coupling facility (CF) activity:
 * structure names, request rates (read/write/invalidate), and CF link
 * utilisation per measurement interval.
 *
 * Subtypes:
 *   1  Coupling facility activity
 *   2  CF structure activity
 *
 * TODO: implement type-specific section parsing against IBM GA32-0869.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace smf {

namespace smf77 {
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

    // Enqueue Data
    MAJOR_NAME    = 200,
    MINOR_NAME    = 201,
    TOTAL_WAIT_MS = 202,
    MAX_WAIT_MS   = 203,
    EVENT_COUNT   = 204,
};
} // namespace smf77

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf77Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf77EnqueueData {
    std::string major_name;      // SMF77QNM (8 bytes)
    std::string minor_name;      // SMF77RNM (44 bytes)
    uint32_t    total_wait_ms{0}; // SMF77WTT (units of 1024 microseconds)
    uint32_t    max_wait_ms{0};   // SMF77WTX
    uint32_t    event_count{0};   // SMF77EVT
};

struct Smf77Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    Smf77Product             product;
    std::vector<Smf77EnqueueData> enqueues;

    [[nodiscard]] mf::FieldValue get_field(smf77::FieldID fid) const {
        using namespace smf77;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf77Product read_smf77_product(mf::Reader& sr) {
    Smf77Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // SMF77MFV
    sr.skip(8);                                // SMF77PRD
    p.interval_start_time = sr.read_mf_time(); // SMF77IST
    p.interval_start_date = sr.read_mf_date(); // SMF77DAT
    p.interval_hund       = sr.read_u32();     // SMF77INT
    p.sample_count        = sr.read_u16();     // SMF77SAM
    sr.skip(1);                                // SMF77FLA
    if (sr.can_read(4)) sr.skip(4);            // SMF77CYC
    if (sr.can_read(8)) sr.skip(8);            // SMF77MVS
    if (sr.can_read(4)) sr.skip(4);            // IML/PRF/PTN/SRL
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF77XNM
    return p;
}

[[nodiscard]] inline Smf77EnqueueData read_smf77_enqueue(mf::Reader& er) {
    Smf77EnqueueData d;
    if (er.can_read(8))  d.major_name = mf::rtrim(er.read_ebcdic(8));   // SMF77QNM
    if (er.can_read(44)) d.minor_name = mf::rtrim(er.read_ebcdic(44));  // SMF77RNM
    er.skip(4); // SMF77WTM
    if (er.can_read(4)) d.max_wait_ms   = (er.read_u32() * 1024) / 1000; // SMF77WTX
    if (er.can_read(4)) d.total_wait_ms = (er.read_u32() * 1024) / 1000; // SMF77WTT
    return d;
}

/* ── Main SMF77 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf77Record read_smf77(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf77Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 1) return out;

    r.pos = 26; // SMF77TRN is at 22, triplets at 26
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Control
    const SectionPtr enq_ptr  = read_section_ptr(r); // [2] Enqueue Data

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf77_product(sr);

    if (enq_ptr.count > 0 && enq_ptr.length >= 60) {
        out.enqueues.reserve(enq_ptr.count);
        for (uint32_t i = 0; i < enq_ptr.count; ++i) {
            const std::size_t off = enq_ptr.offset + static_cast<std::size_t>(i) * enq_ptr.length;
            if (off + enq_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, enq_ptr.length) };
            out.enqueues.push_back(read_smf77_enqueue(er));
        }
    }

    return out;
}

} // namespace smf
