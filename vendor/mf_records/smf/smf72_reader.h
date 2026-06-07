#pragma once
/*
 * smf/smf72_reader.h — SMF Type 72 record parser (parse core): RMF Workload Activity.
 *
 * Type 72 records describe Workload Manager (WLM) activity.
 * Subtype 3: Goal Mode Workload Activity (most relevant for modern z/OS).
 *
 * Record structure (z/OS 3.1, IBM GA32-0869 / SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header
 *   Offset 20-21  Subtype (uint16 BE, = 3 for Goal Mode)
 *   Offset 22+    Section pointer area: sequential 12-byte triplets
 *                 (offset/length/count, IBM standard order)
 *
 * Triplet order for Subtype 3 (SA23-1370):
 *   [0] Product section      (R723PRS/PRL/PRN)
 *   [1] Control section      (R723WMS/WML/WMN) — Policy, Workload, Class names
 *   [2] Served data          (R723SVS) — not parsed
 *   [3] Resource group data  (R723RGS) — optional
 *   [4] Period data          (R723SCS) — Goal vs Actual measured values
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf72/SMF72S3/
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

namespace smf72 {
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

    // Control Section
    POLICY_NAME   = 200,
    WORKLOAD_NAME = 201,
    CLASS_NAME    = 202,
    IS_REPORT_CLASS = 203,

    // Period Section
    PERIOD_NUM    = 300,
    IMPORTANCE    = 301,
    SERVICE_UNITS = 302,
    CPU_UNITS     = 303,
    IOC_UNITS     = 304,
    MSO_UNITS     = 305,
    SRB_UNITS     = 306,
    TRANS_ENDED   = 307,
    TRANS_ELAPSED_MS = 308,
};

[[nodiscard]] inline FieldID smf72_lookup_field(std::string_view name) {
    if (name == "SYSTEM_ID")     return FieldID::SYSTEM_ID;
    if (name == "SUBSYSTEM_ID")  return FieldID::SUBSYSTEM_ID;
    if (name == "RECORD_TYPE")   return FieldID::RECORD_TYPE;
    if (name == "SUBTYPE")       return FieldID::SUBTYPE;
    if (name == "SMF_TIMESTAMP") return FieldID::SMF_TIMESTAMP;
    if (name == "SMF_DATE")      return FieldID::SMF_DATE;

    if (name == "SMF72INT")      return FieldID::INTERVAL_MS;
    if (name == "SMF72SAM")      return FieldID::SAMPLE_COUNT;

    if (name == "R723MNSP")      return FieldID::POLICY_NAME;
    if (name == "R723MWNM")      return FieldID::WORKLOAD_NAME;
    if (name == "R723MCNM")      return FieldID::CLASS_NAME;

    if (name == "R723CPER")      return FieldID::PERIOD_NUM;
    if (name == "R723CIMP")      return FieldID::IMPORTANCE;
    if (name == "R723CSRV")      return FieldID::SERVICE_UNITS;
    if (name == "R723CCPU")      return FieldID::CPU_UNITS;
    if (name == "R723CIOC")      return FieldID::IOC_UNITS;
    if (name == "R723CMSO")      return FieldID::MSO_UNITS;
    if (name == "R723CSRB")      return FieldID::SRB_UNITS;
    if (name == "R723CPRO")      return FieldID::TRANS_ENDED;
    if (name == "R723CTET")      return FieldID::TRANS_ELAPSED_MS;

    return FieldID::NONE;
}
} // namespace smf72

/* ── Intermediate result structures ────────────────────────────────────── */

struct Smf72Product {
    mf::MfTime  interval_start_time{};
    mf::MfDate  interval_start_date{};
    uint32_t    interval_hund{0};
    uint16_t    sample_count{0};
    std::string sysplex_name;
};

struct Smf72Control {
    std::string policy_name;    // R723MNSP: service policy name (8 chars EBCDIC)
    std::string workload_name;  // R723MWNM: workload name (8 chars EBCDIC)
    std::string class_name;     // R723MCNM: service/report class name (8 chars EBCDIC)
    bool        is_report_class{false}; // R723MFLG Bit 0
};

struct Smf72Period {
    uint32_t    period_num{0};    // R723CPER: period number
    uint32_t    importance{0};    // R723CIMP: importance level
    uint64_t    service_units{0}; // R723CSRV: total service units
    uint64_t    cpu_units{0};     // R723CCPU: CPU service units
    uint64_t    ioc_units{0};     // R723CIOC: I/O service units
    uint64_t    mso_units{0};     // R723CMSO: MSO service units
    uint64_t    srb_units{0};     // R723CSRB: SRB service units
    uint32_t    trans_ended{0};   // R723CPRO: transactions ended
    uint64_t    trans_elapsed_hund{0}; // R723CTET: transaction elapsed time (hundredths)
};

struct Smf72Record {
    mf::SmfHeader            header;
    uint16_t                 subtype{0};
    Smf72Product             product;
    Smf72Control             control;
    std::vector<Smf72Period> periods;

    [[nodiscard]] mf::FieldValue get_field(smf72::FieldID fid) const {
        using namespace smf72;
        switch (fid) {
            case FieldID::SYSTEM_ID:     return mf::FieldValue::from_string(header.system_id);
            case FieldID::SUBSYSTEM_ID:  return mf::FieldValue::from_string(header.subsystem_id);
            case FieldID::RECORD_TYPE:   return mf::FieldValue::from_int64(header.record_type);
            case FieldID::SUBTYPE:       return mf::FieldValue::from_int64(subtype);
            case FieldID::INTERVAL_MS:   return mf::FieldValue::from_double(product.interval_hund * 10.0);
            case FieldID::POLICY_NAME:   return mf::FieldValue::from_string(control.policy_name);
            case FieldID::WORKLOAD_NAME: return mf::FieldValue::from_string(control.workload_name);
            case FieldID::CLASS_NAME:    return mf::FieldValue::from_string(control.class_name);
            case FieldID::IS_REPORT_CLASS: return mf::FieldValue::from_int64(control.is_report_class ? 1 : 0);
            default:                     return mf::FieldValue::null();
        }
    }
};

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf72Product read_smf72_product(mf::Reader& sr) {
    Smf72Product p;
    if (!sr.can_read(24)) return p;
    sr.skip(1);                                // SMF72MFV
    sr.skip(8);                                // SMF72PRD
    p.interval_start_time = sr.read_mf_time(); // SMF72IST
    p.interval_start_date = sr.read_mf_date(); // SMF72DAT
    p.interval_hund       = sr.read_u32();     // SMF72INT
    p.sample_count        = sr.read_u16();     // SMF72SAM
    sr.skip(1);                                // SMF72FLA
    if (sr.can_read(4)) sr.skip(4);            // SMF72CYC
    if (sr.can_read(8)) sr.skip(8);            // SMF72MVS
    if (sr.can_read(4)) sr.skip(4);            // IML/PRF/PTN/SRL
    if (sr.can_read(8))
        p.sysplex_name = mf::rtrim(sr.read_ebcdic(8)); // SMF72XNM
    return p;
}

[[nodiscard]] inline Smf72Control read_smf72_control(mf::Reader& sr) {
    Smf72Control c;
    // Workload Control Section (R723WMS)
    if (sr.can_read(8)) c.policy_name = mf::rtrim(sr.read_ebcdic(8));   // offset 0
    if (sr.can_read(8)) c.workload_name = mf::rtrim(sr.read_ebcdic(8)); // offset 8
    if (sr.can_read(8)) c.class_name = mf::rtrim(sr.read_ebcdic(8));    // offset 16
    sr.skip(4); // R723MFLG (offset 24) + padding
    // TODO: verify R723MFLG offset and bit for is_report_class
    return c;
}

[[nodiscard]] inline Smf72Period read_smf72_period(mf::Reader& er) {
    Smf72Period p;
    // Service/Report Class Period Data (R723SCS)
    if (er.can_read(4)) p.period_num = er.read_u32();    // R723CPER
    if (er.can_read(4)) p.importance = er.read_u32();    // R723CIMP
    if (er.can_read(8)) p.service_units = er.read_u64(); // R723CSRV
    if (er.can_read(8)) p.cpu_units = er.read_u64();     // R723CCPU
    if (er.can_read(8)) p.ioc_units = er.read_u64();     // R723CIOC
    if (er.can_read(8)) p.mso_units = er.read_u64();     // R723CMSO
    if (er.can_read(8)) p.srb_units = er.read_u64();     // R723CSRB
    er.skip(8); // R723CSTC
    if (er.can_read(4)) p.trans_ended = er.read_u32();   // R723CPRO
    if (er.can_read(8)) p.trans_elapsed_hund = er.read_u64(); // R723CTET
    return p;
}

/* ── Main SMF72 record parser ───────────────────────────────────────────── */

[[nodiscard]] inline Smf72Record read_smf72(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf72Record out;

    out.header = mf::read_smf_header(r);
    if (r.can_read(2)) out.subtype = r.read_u16();

    if (out.subtype != 3) return out; // only Goal Mode Subtype 3 for now

    // Triplets from offset 22
    const SectionPtr prod_ptr = read_section_ptr(r); // [0] Product
    const SectionPtr ctrl_ptr = read_section_ptr(r); // [1] Control
    const SectionPtr serv_ptr = read_section_ptr(r); // [2] Served data
    const SectionPtr resg_ptr = read_section_ptr(r); // [3] Resource group
    const SectionPtr peri_ptr = read_section_ptr(r); // [4] Period data
    (void)serv_ptr; (void)resg_ptr;

    mf::Reader sr{rec_bytes};

    if (make_section_reader(rec_bytes, prod_ptr, 24, sr))
        out.product = read_smf72_product(sr);

    if (make_section_reader(rec_bytes, ctrl_ptr, 24, sr))
        out.control = read_smf72_control(sr);

    if (peri_ptr.count > 0 && peri_ptr.length >= 68) {
        out.periods.reserve(peri_ptr.count);
        for (uint32_t i = 0; i < peri_ptr.count; ++i) {
            const std::size_t off = peri_ptr.offset + static_cast<std::size_t>(i) * peri_ptr.length;
            if (off + peri_ptr.length > rec_bytes.size()) break;
            mf::Reader er{ rec_bytes.subspan(off, peri_ptr.length) };
            out.periods.push_back(read_smf72_period(er));
        }
    }

    return out;
}

} // namespace smf
