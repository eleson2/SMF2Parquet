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
    for (int i = 0; i < 4; ++i) {
        if (s[i] == '\0') {
            for (; i < 4; ++i) b.push_back(ebcdic_cp037(' '));
            break;
        }
        b.push_back(ebcdic_cp037(s[i]));
    }
}
void put_ebcdic8(std::vector<uint8_t>& b, const char* s) {
    for (int i = 0; i < 8; ++i) {
        if (s[i] == '\0') {
            for (; i < 8; ++i) b.push_back(ebcdic_cp037(' '));
            break;
        }
        b.push_back(ebcdic_cp037(s[i]));
    }
}
void put_triplet(std::vector<uint8_t>& b, uint32_t offset, uint32_t length, uint32_t count) {
    put_u32_be(b, offset);
    put_u32_be(b, length);
    put_u32_be(b, count);
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

void put_header(std::vector<uint8_t>& body, uint8_t type, int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    put_u16_be(body, 0);             // record_len placeholder (filled by caller)
    body.push_back(0x00);           // flags
    body.push_back(type);           // record type
    put_u32_be(body, time_hs);      // time in hundredths of seconds since midnight
    put_packed_yyyyddd(body, year, ddd);
    put_ebcdic4(body, sys_id);
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
                                 int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd, time_hs, sys_id);
    if (has_subtype) put_u16_be(body, subtype);
    return finish(std::move(body));
}

std::vector<uint8_t> make_smf30_record(uint16_t subtype, int year, int ddd, uint32_t time_hs,
                                       const char* job_name, const char* step_name,
                                       uint32_t cpu_hs, uint32_t excp, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 30, year, ddd, time_hs, sys_id);
    put_u16_be(body, subtype);
    
    // Triplets start at offset 22
    // Index 0: Subsystem (ignored)
    // Index 1: Identification (SMF30IOF)
    // Index 2: I/O Activity   (SMF30UOF)
    // Index 3: Completion     (SMF30TOF)
    // Index 4: CPU Accounting (SMF30COF)
    
    uint32_t current_off = 22 + 5 * 12;
    
    put_triplet(body, 0, 0, 0); // 0
    
    uint32_t id_off = current_off;
    put_triplet(body, id_off, 32, 1); // 1
    current_off += 32;
    
    uint32_t io_off = current_off;
    put_triplet(body, io_off, 4, 1); // 2
    current_off += 4;
    
    uint32_t perf_off = current_off;
    put_triplet(body, perf_off, 8, 1); // 3
    current_off += 8;
    
    uint32_t proc_off = current_off;
    put_triplet(body, proc_off, 12, 1); // 4
    current_off += 12;

    // ID section
    put_ebcdic8(body, job_name);     // job
    put_ebcdic8(body, step_name);    // step
    put_ebcdic8(body, "JOB01234");   // id
    put_ebcdic8(body, "MYPROG");     // program

    // IO section
    put_u32_be(body, excp);          // excp

    // Perf section
    put_u32_be(body, cpu_hs + 100);  // elapsed (fake)
    put_u32_be(body, cpu_hs);        // total cpu

    // Proc section
    put_u32_be(body, cpu_hs * 8 / 10); // tcb
    put_u32_be(body, cpu_hs * 2 / 10); // srb
    put_u32_be(body, cpu_hs / 2);      // ziip

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf70_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 70, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype
    
    // Triplets at offset 22:
    // [0] Product
    // [1] Control
    // [2] CPU data
    
    uint32_t current_off = 22 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // Product
    uint32_t prod_off = current_off;
    current_off += 48;
    
    put_triplet(body, current_off, 16, 1); // Control
    uint32_t ctrl_off = current_off;
    current_off += 16;
    
    put_triplet(body, current_off, 16, 1); // CPU data (1 entry)
    uint32_t cpu_off = current_off;
    current_off += 16;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs); // start time
    put_packed_yyyyddd(body, year, ddd); // start date
    put_u32_be(body, 90000); // interval (15 mins)
    put_u16_be(body, 1000); // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0); // cycle
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i])); // mvs level
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1"); // sysplex

    // Control Section
    put_ebcdic4(body, "8561"); // model
    put_u16_be(body, 0); // zaap
    put_u16_be(body, 2); // ziip online
    for(int i=0; i<8; ++i) body.push_back(0);

    // CPU Data Section (1 entry)
    put_u32_be(body, 50000); // wait time
    for(int i=0; i<6; ++i) body.push_back(0);
    for(int i=0; i<3; ++i) body.push_back(0);
    body.push_back(0); // type 0 = GP

    return finish(std::move(body));
}

std::vector<uint8_t> make_rmf_record(uint8_t type, uint16_t subtype, int year, int ddd, uint32_t time_hs,
                                     int n_triplets, int data_len, int n_entries = 1, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd, time_hs, sys_id);
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
        const char* sys = "SYS1";
        for (int intv = 0; intv < 96; ++intv) {
            uint32_t t = intv * 15 * 60 * 100;
            
            // System-level CPU activity
            emit(make_smf70_record(Y, DAY, t, sys));
            
            // Interval records (Subtype 2) for long-running regions
            emit(make_smf30_record(2, Y, DAY, t, "CICSPROD", "CICSSTEP", 5000, 1200, sys));
            emit(make_smf30_record(2, Y, DAY, t, "DB2MSTR",  "DB2STEP",  2000, 500,  sys));
            emit(make_smf30_record(2, Y, DAY, t, "DB2DIST",  "DISTSTEP", 3000, 800,  sys));
            
            // Raw records for CICS (110) and DB2 (101)
            emit(make_record(110, false, 0, Y, DAY, t, sys));
            emit(make_record(101, false, 0, Y, DAY, t, sys));

            // RMF stub records
            emit(make_record(71, false, 0, Y, DAY, t, sys));
            emit(make_record(72, true, 1, Y, DAY, t, sys));
            emit(make_record(73, false, 0, Y, DAY, t, sys));
            emit(make_record(76, true, 1, Y, DAY, t, sys));
            emit(make_record(77, true, 1, Y, DAY, t, sys));
            emit(make_record(78, true, 1, Y, DAY, t, sys));
            emit(make_record(79, false, 0, Y, DAY, t, sys));
            emit(make_record(99, true, 0, Y, DAY, t, sys));
            emit(make_record(113, true, 1, Y, DAY, t, sys));
            
            // Page data sets: 10 entries
            emit(make_rmf_record(75, 1, Y, DAY, t, 2, 80, 10, sys)); 

            // Device activity: 1000 devices
            emit(make_rmf_record(74, 1, Y, DAY, t, 3, 64, 1000, sys));

            // Batch jobs (Subtype 4: Termination) - ~5 per interval
            const char* jobs[] = {"PAYROLL", "BILLING", "BACKUP", "PURGE", "REPORT"};
            for (int i = 0; i < 5; ++i) {
                uint32_t cpu = 1000 + (rand() % 5000);
                uint32_t excp = 500 + (rand() % 2000);
                emit(make_smf30_record(4, Y, DAY, t + (i+1)*60*100, jobs[i], "STEP1", cpu, excp, sys));
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
