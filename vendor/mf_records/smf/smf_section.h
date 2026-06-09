#pragma once
/*
 * smf/smf_section.h — Shared section-navigation helpers for SMF/RMF record parsers.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"

#include <cstdint>
#include <span>

namespace smf {

/*
 * SectionPtr is a project-specific alias for mf::Triplet.
 * It provides standard access to (Offset, Len, Count).
 */
using SectionPtr = mf::Triplet;

/*
 * Returns the number of entries that can actually fit in the record
 * starting from offset, preventing bad_alloc on reserve().
 */
[[nodiscard]] inline uint32_t safe_count(const mf::Triplet& t, std::size_t rec_size) noexcept {
    if (t.count == 0 || t.len == 0 || t.offset >= rec_size) return 0;
    const std::size_t remaining = rec_size - t.offset;
    const std::size_t max_possible = remaining / t.len;
    return (t.count < max_possible) ? t.count : static_cast<uint32_t>(max_possible);
}

/* 
 * Create a new Reader positioned at a section's start.
 * Validates that the section exists and fits within the record.
 */
[[nodiscard]] inline bool make_section_reader(std::span<const std::byte> rec_bytes,
                                              const mf::Triplet&          p,
                                              uint32_t                   min_len,
                                              mf::Reader&                sr) {
    if (p.count == 0 || p.len < min_len) return false;
    if (static_cast<std::size_t>(p.offset) + p.len > rec_bytes.size()) return false;
    sr = mf::Reader{ rec_bytes.subspan(p.offset, p.len) };
    return true;
}

[[nodiscard]] inline SectionPtr read_section_ptr(mf::Reader& r) {
    return mf::read_triplet(r);
}

/*════════════════════════════════════════════════════════════════
  Common Field Identifiers (for get_field metadata access)
════════════════════════════════════════════════════════════════*/

enum class CommonFieldID : uint32_t {
    NONE          = 0,
    SYSTEM_ID     = 1,
    SUBSYSTEM_ID  = 2,
    RECORD_TYPE   = 3,
    SUBTYPE       = 4,
    SMF_TIMESTAMP = 5,
    SMF_DATE      = 6,
};

} // namespace smf
