/*
 * tests/gen_test_smf.cpp — write RECFM=VB SMF test files.
 *
 * Usage: gen_test_smf <out.smf> [small|large]
 *
 * Profile 'small':
 *   - 5 records for every handled type.
 *
 * Profile 'large':
 *   - Mimics a 24-hour period for one 600 MIPS LPAR.
 *   - 96 intervals (15 mins each).
 *   - Interval-based records (70-73, 75-79, 99, 113) emitted once per interval.
 *   - Type 74 (Device Activity) emitted for 200 devices per interval.
 *   - Type 30 (Common Address Space) emitted ~10 times per interval.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace {

// ASCII -> EBCDIC CP037 for the characters used in system/subsystem ids.
uint8_t ebcdic_cp037(char c) {
    if (c == ' ')               return 0x40;
    if (c >= '0' && c <= '9')   return 0xF0 + (c - '0');
    if (c >= 'a' && c <= 'i')   return 0x81 + (c - 'a');
    if (c >= 'j' && c <= 'r')   return 0x91 + (c - 'j');
    if (c >= 's' && c <= 'z')   return 0xA2 + (c - 's');
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
// 4-byte packed decimal (COMP-3) YYYYDDD with sign nibble 0xC.
void put_packed_yyyyddd(std::vector<uint8_t>& b, int year, int ddd) {
    int v = year * 1000 + ddd;
    // v is e.g. 2025100. We need 7 digits: 2, 0, 2, 5, 1, 0, 0.
    // Packed: 0x20 0x25 0x10 0x0C
    uint8_t p[4];
    p[3] = static_cast<uint8_t>(((v % 10) << 4) | 0xC); v /= 10;
    p[2] = static_cast<uint8_t>(((v / 10 % 10) << 4) | (v % 10)); v /= 100;
    p[1] = static_cast<uint8_t>(((v / 10 % 10) << 4) | (v % 10)); v /= 100;
    p[0] = static_cast<uint8_t>(((v / 10 % 10) << 4) | (v % 10));
    for (int i = 0; i < 4; ++i) b.push_back(p[i]);
}

void put_header(std::vector<uint8_t>& body, uint8_t type, int year, int ddd, uint32_t time_hs) {
    put_u16_be(body, 0);             // record_len placeholder (filled by caller)
    body.push_back(0x00);           // flags
    body.push_back(type);           // record type
    put_u32_be(body, time_hs);      // time in hundredths of seconds since midnight
    put_packed_yyyyddd(body, year, ddd);
    put_ebcdic4(body, "SYS1");
    put_ebcdic4(body, "JES2");
}

std::vector<uint8_t> finish(std::vector<uint8_t> body) {
    body[0] = static_cast<uint8_t>(body.size() >> 8);
    body[1] = static_cast<uint8_t>(body.size());
    std::vector<uint8_t> rec;
    put_u16_be(rec, static_cast<uint16_t>(body.size() + 4));
    put_u16_be(rec, 0);
    rec.insert(rec.end(), body.begin(), body.end());
    return rec;
}

std::vector<uint8_t> make_record(uint8_t type, bool has_subtype, uint16_t subtype,
                                 int year, int ddd, uint32_t time_hs) {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd, time_hs);
    if (has_subtype) put_u16_be(body, subtype);
    return finish(std::move(body));
}

std::vector<uint8_t> make_rmf_record(uint8_t type, uint16_t subtype, int year, int ddd, uint32_t time_hs,
                                     int n_triplets, int data_len, int n_entries = 1) {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd, time_hs);
    put_u16_be(body, subtype);
    put_u32_be(body, static_cast<uint32_t>(n_entries));   // SMF*TRN
    const uint32_t data_off = 26u + static_cast<uint32_t>(n_triplets) * 12u;
    for (int i = 0; i < n_triplets; ++i) {
        if (i == n_triplets - 1) {
            put_u32_be(body, data_off);
            put_u32_be(body, static_cast<uint32_t>(data_len));
            put_u32_be(body, static_cast<uint32_t>(n_entries));
        } else {
            put_u32_be(body, 0); put_u32_be(body, 0); put_u32_be(body, 0);
        }
    }
    for (int i = 0; i < data_len * n_entries; ++i) body.push_back(0x40);
    return finish(std::move(body));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <out.smf> [small|large]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    std::string profile = (argc >= 3) ? argv[2] : "small";

    constexpr int Y = 2025;
    constexpr int DAY = 100;

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return EXIT_FAILURE; }

    auto emit = [&](std::vector<uint8_t> rec) {
        std::fwrite(rec.data(), 1, rec.size(), f);
    };

    if (profile == "small") {
        for (int i = 0; i < 2; ++i) {
            int ddd = DAY + i;
            const char* sys = (i == 0) ? "SYS1" : "SYS2";
            uint32_t t = 360000;
            
            auto emit_small = [&](uint8_t type, bool has_sub, uint16_t sub) {
                std::vector<uint8_t> body;
                put_u16_be(body, 0);
                body.push_back(0x00);
                body.push_back(type);
                put_u32_be(body, t);
                put_packed_yyyyddd(body, Y, ddd);
                put_ebcdic4(body, sys);
                put_ebcdic4(body, "JES2");
                if (has_sub) put_u16_be(body, sub);
                emit(finish(std::move(body)));
            };

            auto emit_rmf_small = [&](uint8_t type, uint16_t sub, int triplets, int len, int entries) {
                std::vector<uint8_t> body;
                put_u16_be(body, 0);
                body.push_back(0x00);
                body.push_back(type);
                put_u32_be(body, t);
                put_packed_yyyyddd(body, Y, ddd);
                put_ebcdic4(body, sys);
                put_ebcdic4(body, "JES2");
                put_u16_be(body, sub);
                put_u32_be(body, static_cast<uint32_t>(entries));
                uint32_t off = 26u + triplets * 12u;
                for (int j = 0; j < triplets; ++j) {
                    if (j == triplets - 1) {
                        put_u32_be(body, off); put_u32_be(body, len); put_u32_be(body, entries);
                    } else {
                        put_u32_be(body, 0); put_u32_be(body, 0); put_u32_be(body, 0);
                    }
                }
                for (int j = 0; j < len * entries; ++j) body.push_back(0x40);
                emit(finish(std::move(body)));
            };

            // Type 30: Subtypes 1 (Job Start), 4 (Step End), 5 (Job End)
            emit_small(30, true, 1);
            emit_small(30, true, 4);
            emit_small(30, true, 5);

            emit_small(70, true, 1);
            emit_small(71, false, 0);
            emit_small(72, true, 1);
            emit_small(73, false, 0);
            emit_rmf_small(74, 1, 3, 32, 2); // 2 devices
            emit_rmf_small(75, 1, 2, 80, 2); // 2 pagesets
            emit_small(76, true, 1);
            emit_small(77, true, 1);
            emit_small(78, true, 1);
            emit_small(79, false, 0);
            emit_small(99, true, 0);
            emit_small(113, true, 1);
            emit_small(250, false, 0);
        }
    } else if (profile == "large") {
        // 24 hours = 96 intervals of 15 mins
        for (int intv = 0; intv < 96; ++intv) {
            uint32_t t = intv * 15 * 60 * 100;
            // Interval records
            emit(make_record(70, true, 1, Y, DAY, t));
            emit(make_record(71, false, 0, Y, DAY, t));
            emit(make_record(72, true, 1, Y, DAY, t));
            emit(make_record(73, false, 0, Y, DAY, t));
            emit(make_record(76, true, 1, Y, DAY, t));
            emit(make_record(77, true, 1, Y, DAY, t));
            emit(make_record(78, true, 1, Y, DAY, t));
            emit(make_record(79, false, 0, Y, DAY, t));
            emit(make_record(99, true, 0, Y, DAY, t));
            emit(make_record(113, true, 1, Y, DAY, t));
            
            // Page data sets: 10 entries
            emit(make_rmf_record(75, 1, Y, DAY, t, 2, 80, 10)); 

            // Device activity: 1000 devices
            emit(make_rmf_record(74, 1, Y, DAY, t, 3, 64, 1000));

            // Type 30 records: 100 per interval
            for (int i = 0; i < 100; ++i) {
                emit(make_record(30, true, 4, Y, DAY, t + i * 10));
            }
        }
    } else if (profile == "edge") {
        uint32_t t = 120000;
        // 1. Malformed triplet: offset way out of bounds
        // (Note: make_rmf_record uses n_triplets * 12 + 26 as base offset, 
        // we manually construct one here)
        {
            std::vector<uint8_t> body;
            put_header(body, 74, Y, DAY, t);
            put_u16_be(body, 1);    // subtype
            put_u32_be(body, 1);    // SMF*TRN
            put_u32_be(body, 9999); // offset: out of bounds
            put_u32_be(body, 32);   // len
            put_u32_be(body, 1);    // count
            emit(finish(std::move(body)));
        }

        // 2. Zero-entry section
        emit(make_rmf_record(74, 1, Y, DAY, t + 100, 3, 32, 0));

        // 3. Max length record (32767 bytes total)
        // RDW(4) + Body(32763)
        // Body: Header(20) + Subtype(2) + TRN(4) + 1 triplet(12) = 38
        // Data entries: (32763 - 38) / 32 = 1022.6...
        int huge_len = 32763 - 38;
        emit(make_rmf_record(74, 1, Y, DAY, t + 200, 1, huge_len, 1));

        // 4. Large number of entries
        emit(make_rmf_record(74, 1, Y, DAY, t + 300, 3, 32, 1000));

    } else {
        std::fprintf(stderr, "Unknown profile: %s\n", profile.c_str());
        std::fclose(f);
        return EXIT_FAILURE;
    }

    std::fclose(f);
    std::printf("Generated %s profile to %s\n", profile.c_str(), path);

    return EXIT_SUCCESS;
}
