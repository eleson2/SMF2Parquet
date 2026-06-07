#pragma once
/*
 * smf_reader.h - SMF Record Header
 *
 * Parses the standard 20-byte prefix present on every SMF record.
 * Generic mainframe types (MfTime, MfDate, TOD clock) live in mf_types.h
 * and are accessible via mf::Reader methods directly.
 *
 * Standard SMF record header layout:
 *
 *   Offset  Len  Encoding        Field
 *   0       2    uint16 BE       Total record length (including these 2 bytes)
 *   2       1    uint8           Flag byte  (SmfHeader::flags)
 *   3       1    uint8           Record type, e.g. 14, 30, 70, 80
 *   4       4    uint32 BE       Time: hundredths of seconds since midnight
 *   8       4    COMP-3 PL4      Date: packed decimal YYYYDDD
 *   12      4    EBCDIC          System ID    (4 chars)
 *   16      4    EBCDIC          Subsystem ID (4 chars)
 *   ──── cursor is at byte 20 after read_smf_header() returns ────
 *   20      2    uint16 BE       Subtype (only on subtype-capable record types)
 *
 * Record-type-specific parsers should be implemented separately
 * (e.g. smf30_reader.h, smf70_reader.h) using mf::Reader methods.
 */

#include "dataset_reader.h"

namespace mf {

struct SmfHeader {
    uint32_t    record_len;      /* total record length in bytes             */
    uint8_t     flags;           /* SMFRECFL flag byte                       */
    uint16_t    record_type;     /* SMF record type (can be > 255)           */
    MfTime      time;            /* creation time                            */
    MfDate      date;            /* creation date                            */
    std::string system_id;       /* SMFSID,  4 chars, decoded from EBCDIC    */
    std::string subsystem_id;    /* SMFSSID, 4 chars, decoded from EBCDIC    */
};

/*
 * Parse the standard SMF header.
 *
 * If the record length (first 2 bytes) is X'FFFF', it's an extended header
 * where the type is 2 bytes at offset 2, and the length is 4 bytes at offset 4.
 * Otherwise, it's a standard header (type at offset 5, length 2 bytes at offset 0).
 *
 * Cursor is left at the start of the subtype word (if any) on return.
 */
[[nodiscard]] constexpr SmfHeader read_smf_header(Reader& r) {
    SmfHeader h;
    const uint16_t raw_len = r.read_u16(); // offset 0
    
    if (raw_len == 0xFFFFu) {
        // Extended Header (z/OS 2.1+)
        h.record_type  = r.read_u16();     // offset 2
        h.record_len   = r.read_u32();     // offset 4
        h.flags        = r.read_u8();      // offset 8
        r.skip(1);                         // Reserved
        h.time         = r.read_mf_time(); // offset 10
        h.date         = r.read_mf_date(); // offset 14
        h.system_id    = r.read_ebcdic_trimmed(4); // offset 18
        h.subsystem_id = r.read_ebcdic_trimmed(4); // offset 22
        r.pos = 32; // Extended header is 32 bytes
    } else {
        // Standard Header
        h.record_len   = raw_len;
        h.flags        = r.read_u8();      // offset 2
        h.record_type  = r.read_u8();      // offset 3
        h.time         = r.read_mf_time(); // offset 4
        h.date         = r.read_mf_date(); // offset 8
        h.system_id    = r.read_ebcdic_trimmed(4); // offset 12
        h.subsystem_id = r.read_ebcdic_trimmed(4); // offset 16
        r.pos = 20; // Standard header is 20 bytes
    }
    return h;
}

/* ── Section triplet (used by all subtype-capable SMF record types) ──────── */

struct Triplet {
    uint32_t offset;  /* byte offset from record start (byte 0) */
    uint32_t count;   /* number of section instances            */
    uint32_t len;     /* length of each instance in bytes       */
};

/* Read one 12-byte triplet and advance the cursor */
[[nodiscard]] constexpr Triplet read_triplet(Reader& r) {
    return { r.read_u32(), r.read_u32(), r.read_u32() };
}

} // namespace mf
