#pragma once
/*
 * smf/smf_section.h — Shared section-navigation helpers for SMF/RMF record parsers.
 *
 * IBM SMF records use a self-describing "triplet" layout:
 *   uint32  offset  — byte offset from start of record to the section data
 *   uint32  length  — byte length of each section entry
 *   uint32  count   — number of section entries (0 = section absent)
 * Total: 12 bytes per triplet, in IBM-standard order (offset / length / count).
 *
 * Usage:
 *   const SectionPtr p = read_section_ptr(r);   // reads 12 bytes, advances r
 *   mf::Reader sr{rec_bytes};
 *   if (make_section_reader(rec_bytes, p, min_bytes, sr))
 *       // sr is now positioned at the first byte of the section entry
 *
 * This header lives in the MF-records-to-C library (namespace smf) so that both
 * the ClickHouse back end (SMF-parser) and the Parquet back end (SMF2Parquet)
 * share one definition.
 */

#include "../dataset_reader.h"

#include <cstdint>
#include <span>

namespace smf {

/* ── Common Field IDs (Header + Metadata) ────────────────────────────────── */

enum class CommonFieldID : uint32_t {
    NONE = 0,
    SYSTEM_ID = 1,
    SUBSYSTEM_ID = 2,
    RECORD_TYPE = 3,
    SUBTYPE = 4,
    SMF_TIMESTAMP = 5,
    SMF_DATE = 6,
    // Add more as needed (e.g. flags)
};

struct SectionPtr {
    uint32_t offset{0};  // byte offset from record start to section data
    uint32_t length{0};  // byte length of each section entry
    uint32_t count{0};   // number of entries (0 = section absent)
};

// Read one 12-byte triplet from the current reader position.
// Returns a zero SectionPtr if fewer than 12 bytes remain.
[[nodiscard]] inline SectionPtr read_section_ptr(mf::Reader& r) {
    if (!r.can_read(12)) return SectionPtr{};
    SectionPtr p;
    p.offset = r.read_u32();  // IBM order: offset first
    p.length = r.read_u32();  // then length per entry
    p.count  = r.read_u32();  // then number of entries
    return p;
}

// Create a sub-reader scoped to the first entry of a section.
// Returns true and positions sr at section start if the triplet is valid
// and the first entry has at least min_length bytes.
// Returns false if the section is absent (count==0), entry is too small,
// or the section overruns the record.
[[nodiscard]] inline bool make_section_reader(
    std::span<const std::byte> rec_bytes,
    const SectionPtr&          p,
    uint32_t                   min_length,
    mf::Reader&                sr)
{
    if (p.count == 0 || p.length < min_length) return false;
    if (static_cast<std::size_t>(p.offset) + p.length > rec_bytes.size()) return false;
    sr = mf::Reader{ rec_bytes.subspan(p.offset, p.length) };
    return true;
}

} // namespace smf
