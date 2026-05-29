/*
 * tests/gen_test_smf.cpp — write a small RECFM=VB SMF test file (self-contained).
 *
 * No dependency on the parser or Arrow — it just emits bytes in the SMF wire format
 * the vendored cores expect, so SMF2Parquet has its own test-data generator.
 *
 * Usage: gen_test_smf [out.smf]        (default: test.smf)
 *
 * Emits all 13 handled types (30, 70-79, 99, 113) + one unknown type (250).
 *   - Types 74 & 75 are multi-row (one row per device / page data set); they get a
 *     record with one data-section entry so their tables are populated.
 *   - Type 30 and the unknown type also appear on a SECOND date, so the partitioned
 *     writer is exercised across `smf_date=` partitions (midnight-straddle case).
 *
 * Record wire format (see DESIGN §4):
 *   RDW(4): total len BE (incl RDW), then 2 flag bytes (0)
 *   body : record_len BE(2), flags(1), type(1), time BE u32 (hundredths since midnight),
 *          date packed-decimal YYYYDDD (4 bytes, sign nibble 0xC),
 *          system_id(4 EBCDIC), subsystem_id(4 EBCDIC), [subtype BE u16 if present]
 * RMF section records additionally carry: SMF*TRN BE u32, then 12-byte triplets
 *          (offset/length/count), then the section data.
 *
 * Field VALUES in the 74/75 data sections are placeholders (the parse-core field
 * offsets are still unverified — TODO Phase 7); this fixture validates structure,
 * partitioning, and the multi-row path, not field accuracy.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// ASCII -> EBCDIC CP037 for the characters used in system/subsystem ids.
uint8_t ebcdic_cp037(char c) {
    if (c == ' ')               return 0x40;
    if (c >= '0' && c <= '9')   return 0xF0 + (c - '0');
    if (c >= 'A' && c <= 'I')   return 0xC1 + (c - 'A');
    if (c >= 'J' && c <= 'R')   return 0xD1 + (c - 'J');
    if (c >= 'S' && c <= 'Z')   return 0xE2 + (c - 'S');
    return 0x40;  // pad unknown as space
}

void put_u16_be(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void put_u32_be(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void put_ebcdic4(std::vector<uint8_t>& b, const char* s) {
    for (int i = 0; i < 4; ++i) b.push_back(ebcdic_cp037(s[i] ? s[i] : ' '));
}
// 7-digit YYYYDDD as 4-byte packed decimal with sign nibble 0xC.
void put_packed_yyyyddd(std::vector<uint8_t>& b, int year, int ddd) {
    int v = year * 1000 + ddd;
    int d[7];
    for (int i = 6; i >= 0; --i) { d[i] = v % 10; v /= 10; }
    b.push_back(static_cast<uint8_t>((d[0] << 4) | d[1]));
    b.push_back(static_cast<uint8_t>((d[2] << 4) | d[3]));
    b.push_back(static_cast<uint8_t>((d[4] << 4) | d[5]));
    b.push_back(static_cast<uint8_t>((d[6] << 4) | 0xC));
}

void put_header(std::vector<uint8_t>& body, uint8_t type, int year, int ddd) {
    put_u16_be(body, 0);             // record_len placeholder (filled by caller)
    body.push_back(0x00);           // flags
    body.push_back(type);           // record type
    put_u32_be(body, 360000);       // time 01:00:00.00
    put_packed_yyyyddd(body, year, ddd);
    put_ebcdic4(body, "SYS1");
    put_ebcdic4(body, "JES2");
}

// Finalise: set record_len, prepend the 4-byte RDW.
std::vector<uint8_t> finish(std::vector<uint8_t> body) {
    body[0] = static_cast<uint8_t>(body.size() >> 8);
    body[1] = static_cast<uint8_t>(body.size());
    std::vector<uint8_t> rec;
    put_u16_be(rec, static_cast<uint16_t>(body.size() + 4));
    put_u16_be(rec, 0);
    rec.insert(rec.end(), body.begin(), body.end());
    return rec;
}

// Plain header (+ optional subtype) record.
std::vector<uint8_t> make_record(uint8_t type, bool has_subtype, uint16_t subtype,
                                 int year, int ddd) {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd);
    if (has_subtype) put_u16_be(body, subtype);
    return finish(std::move(body));
}

// RMF section record: subtype + SMF*TRN + n_triplets, with the LAST triplet pointing
// to a single data-section entry of data_len bytes (filled with EBCDIC spaces so
// string fields rtrim to empty and numeric fields are non-garbage-but-arbitrary).
std::vector<uint8_t> make_rmf_record(uint8_t type, uint16_t subtype, int year, int ddd,
                                     int n_triplets, int data_len) {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd);
    put_u16_be(body, subtype);
    put_u32_be(body, static_cast<uint32_t>(n_triplets));   // SMF*TRN
    const uint32_t data_off = 26u + static_cast<uint32_t>(n_triplets) * 12u;  // 20 hdr + 2 sub + 4 trn
    for (int i = 0; i < n_triplets; ++i) {
        if (i == n_triplets - 1) {                         // data section triplet
            put_u32_be(body, data_off);
            put_u32_be(body, static_cast<uint32_t>(data_len));
            put_u32_be(body, 1);                           // one entry
        } else {                                           // absent product/control
            put_u32_be(body, 0); put_u32_be(body, 0); put_u32_be(body, 0);
        }
    }
    for (int i = 0; i < data_len; ++i) body.push_back(0x40);  // EBCDIC spaces
    return finish(std::move(body));
}

} // namespace

int main(int argc, char* argv[]) {
    const char* path = (argc >= 2) ? argv[1] : "test.smf";

    constexpr int Y = 2025;
    constexpr int DAY_A = 89;   // 2025-03-30
    constexpr int DAY_B = 90;   // 2025-03-31

    std::vector<uint8_t> file;
    auto add = [&file](std::vector<uint8_t> rec) { file.insert(file.end(), rec.begin(), rec.end()); };

    std::size_t n_records = 0;
    auto emit = [&](std::vector<uint8_t> rec) { add(std::move(rec)); ++n_records; };

    // Single-row / header-only types.
    struct Spec { uint8_t type; bool sub; uint16_t st; int ddd; };
    const Spec specs[] = {
        {30,  true,  1, DAY_A}, {70,  true, 1, DAY_A}, {71,  false, 0, DAY_A},
        {72,  true,  1, DAY_A}, {73,  false, 0, DAY_A}, {76,  true,  1, DAY_A},
        {77,  true,  1, DAY_A}, {78,  true, 1, DAY_A}, {79,  false, 0, DAY_A},
        {99,  true,  0, DAY_A}, {113, true, 1, DAY_A}, {250, false, 0, DAY_A},
        {30,  true,  3, DAY_B},                          // second date → smf30 spans two partitions
        {250, false, 0, DAY_B},                          // second date → raw spans two partitions
    };
    for (const auto& s : specs) emit(make_record(s.type, s.sub, s.st, Y, s.ddd));

    // Multi-row RMF types: one data-section entry each → one output row.
    emit(make_rmf_record(74, 1, Y, DAY_A, /*triplets=*/3, /*data_len=*/32));  // device data
    emit(make_rmf_record(75, 1, Y, DAY_A, /*triplets=*/2, /*data_len=*/80));  // page data set

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return EXIT_FAILURE; }
    const std::size_t n = std::fwrite(file.data(), 1, file.size(), f);
    std::fclose(f);
    if (n != file.size()) {
        std::fprintf(stderr, "write error: %zu of %zu bytes\n", n, file.size());
        return EXIT_FAILURE;
    }

    std::printf("Wrote %zu bytes (%zu records) to %s\n", file.size(), n_records, path);
    std::printf("Types 30,70-79,99,113 + unknown 250; 30 & 250 also on a second date.\n");
    return EXIT_SUCCESS;
}
