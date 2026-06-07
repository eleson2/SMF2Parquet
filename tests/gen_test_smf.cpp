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
inline void put_u32_be(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v >> 24));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

inline void put_u64_be(std::vector<uint8_t>& b, uint64_t v) {
    put_u32_be(b, static_cast<uint32_t>(v >> 32));
    put_u32_be(b, static_cast<uint32_t>(v));
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

void put_header(std::vector<uint8_t>& body, uint16_t type, int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    if (type <= 255) {
        put_u16_be(body, 0);             // 0-1 record_len (filled by finish)
        body.push_back(0x00);           // 2 flags
        body.push_back(static_cast<uint8_t>(type)); // 3 record type
        put_u32_be(body, time_hs);      // 4-7
        put_packed_yyyyddd(body, year, ddd); // 8-11
        put_ebcdic4(body, sys_id);      // 12-15
        put_ebcdic4(body, "RMF ");      // 16-19
    } else {
        put_u16_be(body, 0xFFFFu);       // 0-1 Extended Header indicator
        put_u16_be(body, type);          // 2-3 record type
        put_u32_be(body, 0);             // 4-7 record_len (filled by finish)
        body.push_back(0x00);           // 8 flags
        body.push_back(0x00);           // 9 reserved
        put_u32_be(body, time_hs);      // 10-13
        put_packed_yyyyddd(body, year, ddd); // 14-17
        put_ebcdic4(body, sys_id);      // 18-21
        put_ebcdic4(body, "RMF ");      // 22-25
        for(int i=0; i<6; ++i) body.push_back(0); // 26-31 reserved
    }
}

std::vector<uint8_t> finish(std::vector<uint8_t> body) {
    if (body[0] == 0xFF && body[1] == 0xFF) {
        // Extended Header: length at offset 4
        uint32_t len = static_cast<uint32_t>(body.size());
        body[4] = static_cast<uint8_t>(len >> 24);
        body[5] = static_cast<uint8_t>(len >> 16);
        body[6] = static_cast<uint8_t>(len >> 8);
        body[7] = static_cast<uint8_t>(len);
    } else {
        // Standard Header: length at offset 0
        body[0] = static_cast<uint8_t>(body.size() >> 8);
        body[1] = static_cast<uint8_t>(body.size());
    }
    
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
    while (body.size() < 32) body.push_back(0x00);
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

std::vector<uint8_t> make_smf71_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 71, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1

    // Triplets at offset 24 (4-2-2)
    // [0] Product (off 24)
    // [1] Paging  (off 32)
    // [2] Swap    (off 40)
    
    uint32_t current_off = 48; // after 3 triplets
    
    // Product
    put_u32_be(body, current_off); put_u16_be(body, 48); put_u16_be(body, 1);
    current_off += 48;
    
    // Paging
    put_u32_be(body, current_off); put_u16_be(body, 64); put_u16_be(body, 1);
    current_off += 64;

    // Swap
    put_u32_be(body, 0); put_u16_be(body, 0); put_u16_be(body, 0);

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0);
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i]));
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1");

    // Paging Section
    put_u32_be(body, 500); // total page ins
    put_u32_be(body, 200); // total page outs
    for(int i=0; i<8; ++i) body.push_back(0); // VIO
    put_u32_be(body, 100000); // avg avail
    put_u32_be(body, 150000); // max avail
    put_u32_be(body, 80000);  // min avail
    put_u32_be(body, 0);      // PFR
    put_u32_be(body, 20000);  // fixed frames
    for(int i=0; i<32; ++i) body.push_back(0); // rest of 64 bytes

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf72_3_record(int year, int ddd, uint32_t time_hs,
                                         const char* workload, const char* service_class,
                                         int n_periods, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 72, year, ddd, time_hs, sys_id);
    put_u16_be(body, 3); // subtype 3 (Goal Mode)

    // Triplets at offset 22:
    // [0] Product
    // [1] Control
    // [2] Served data
    // [3] Resource group
    // [4] Period data
    
    uint32_t current_off = 22 + 5 * 12;
    put_triplet(body, current_off, 48, 1); // Product
    uint32_t prod_off = current_off;
    current_off += 48;
    
    put_triplet(body, current_off, 32, 1); // Control
    uint32_t ctrl_off = current_off;
    current_off += 32;

    put_triplet(body, 0, 0, 0); // Served
    put_triplet(body, 0, 0, 0); // Resource group
    
    put_triplet(body, current_off, 68, n_periods); // Period data
    uint32_t peri_off = current_off;
    current_off += 68 * n_periods;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0);
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i]));
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1");

    // Control Section
    put_ebcdic8(body, "POLICY1");
    put_ebcdic8(body, workload);
    put_ebcdic8(body, service_class);
    put_u32_be(body, 0); // flags/padding (24-27)
    put_u32_be(body, 0); // extra padding to reach 32 bytes (28-31)

    // Period Section(s)
    for (int i = 0; i < n_periods; ++i) {
        put_u32_be(body, i + 1); // period num (0-3)
        put_u32_be(body, 2);     // importance (4-7)
        for (int j = 0; j < 5; ++j) {
            put_u32_be(body, 0); // service units (high 32)
            put_u32_be(body, 1000 * (i + 1)); // service units (low 32)
        }
        // total so far: 8 + 40 = 48
        put_u32_be(body, 0); // STC (high) (48-51)
        put_u32_be(body, 0); // STC (low) (52-55)
        put_u32_be(body, 50); // transactions ended (56-59)
        put_u32_be(body, 0);  // elapsed (high) (60-63)
        put_u32_be(body, 5000); // elapsed (low) (64-67)
        // total: 68 bytes per period.
    }

    return finish(std::move(body));
}
std::vector<uint8_t> make_smf70_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 70, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype
    // [1] Control
    // [2] CPU data
    
    uint32_t current_off = 22 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // Product
    uint32_t prod_off = current_off;
    current_off += 48;
    
    put_triplet(body, current_off, 32, 1); // Control
    uint32_t ctrl_off = current_off;
    current_off += 32;
    
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
    put_ebcdic4(body, "8561"); // model (off 0)
    for(int i=0; i<18; ++i) body.push_back(0); // skip to 22
    put_u16_be(body, 0); // zaap (off 22)
    put_u16_be(body, 2); // ziip online (off 24)
    for(int i=0; i<6; ++i) body.push_back(0); // pad to 32

    // CPU Data Section (1 entry)
    put_u32_be(body, 50000); // wait time
    for(int i=0; i<6; ++i) body.push_back(0);
    for(int i=0; i<3; ++i) body.push_back(0);
    body.push_back(0); // type 0 = GP

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf73_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 73, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1

    // Triplets at offset 22
    // [0] Product
    // [1] Control
    // [2] CHPID Data
    
    uint32_t current_off = 22 + 3 * 12;
    
    put_triplet(body, current_off, 48, 1); // Product
    current_off += 48;
    
    put_triplet(body, 0, 0, 0); // Control (stub)
    
    put_triplet(body, current_off, 32, 2); // CHPID Data (2 entries)
    current_off += 32 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0);
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i]));
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1");

    // CHPID Data Section (2 entries)
    // Entry 1: CHPID 10 (Valid)
    body.push_back(0x10); // ID
    body.push_back(0x20); // Type
    body.push_back(0x02); // Flags (Valid bit)
    body.push_back(0x00); // Flags
    put_u32_be(body, 500); // busy
    for(int i=0; i<8; ++i) body.push_back(0);
    put_ebcdic4(body, "OSD ");
    for(int i=0; i<12; ++i) body.push_back(0); // padding to 32

    // Entry 2: CHPID 20 (Invalid)
    body.push_back(0x20); // ID
    body.push_back(0x10); // Type
    body.push_back(0x00); // Flags (NOT Valid)
    body.push_back(0x00); // Flags
    put_u32_be(body, 999); // busy
    for(int i=0; i<8; ++i) body.push_back(0);
    put_ebcdic4(body, "FCTC");
    for(int i=0; i<12; ++i) body.push_back(0); // padding to 32

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf77_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 77, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1
    put_u32_be(body, 3); // SMF77TRN: 3 triplets
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // Control
    put_triplet(body, current_off, 64, 2); // Enqueue Data (2 entries)
    current_off += 64 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0);
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i]));
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1");

    // Enqueue Data Section (2 entries)
    // Entry 1: SYSDSN
    put_ebcdic8(body, "SYSDSN  ");
    for(int i=0; i<44; ++i) body.push_back(ebcdic_cp037("SYS1.PROCLIB"[i%12]));
    put_u32_be(body, 100); // wtm
    put_u32_be(body, 500); // wtx
    put_u32_be(body, 10000); // wtt
    for(int i=0; i<4; ++i) body.push_back(0);

    // Entry 2: SYSIGGV2
    put_ebcdic8(body, "SYSIGGV2");
    for(int i=0; i<44; ++i) body.push_back(ebcdic_cp037("USER.CATALOG"[i%12]));
    put_u32_be(body, 50); // wtm
    put_u32_be(body, 200); // wtx
    put_u32_be(body, 5000); // wtt
    for(int i=0; i<4; ++i) body.push_back(0);

    return finish(std::move(body));
}
std::vector<uint8_t> make_smf78_3_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 78, year, ddd, time_hs, sys_id);
    put_u16_be(body, 3); // subtype 3
    for(int i=0; i<6; ++i) body.push_back(0); // padding to reach offset 28

    uint32_t current_off = 28 + 3 * 12; // Triplet area (36 bytes) starts at 28
    put_triplet(body, current_off, 48, 1); // Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // Config
    put_triplet(body, current_off, 32, 2); // LCU Data (2 entries)
    current_off += 32 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    body.push_back(0); // flags
    for(int i=0; i<4; ++i) body.push_back(0);
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("z/OS 3.1"[i]));
    for(int i=0; i<4; ++i) body.push_back(0);
    put_ebcdic8(body, "PLEX1");

    // LCU Data Section (2 entries)
    // Entry 1: LCU 0001
    put_u16_be(body, 0x0001); // ID
    body.push_back(0x00);    // CSS
    for(int i=0; i<13; ++i) body.push_back(0);
    put_u32_be(body, 500);   // queue_len_sum
    put_u32_be(body, 1000);  // queue_len_count
    for(int i=0; i<8; ++i) body.push_back(0);

    // Entry 2: LCU 0002
    put_u16_be(body, 0x0002); // ID
    body.push_back(0x01);    // CSS
    for(int i=0; i<13; ++i) body.push_back(0);
    put_u32_be(body, 200);   // queue_len_sum
    put_u32_be(body, 1000);  // queue_len_count
    for(int i=0; i<8; ++i) body.push_back(0);

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf74_4_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 74, year, ddd, time_hs, sys_id);
    put_u16_be(body, 4); // subtype 4
    put_u32_be(body, 5); // TRN
    
    uint32_t current_off = 26 + 5 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, current_off, 160, 1); // [1] Local CF
    current_off += 160;
    put_triplet(body, 0, 0, 0); // [2] Connectivity
    put_triplet(body, 0, 0, 0); // [3] Storage
    put_triplet(body, current_off, 32, 2); // [4] Structure (2 entries)
    current_off += 32 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // Local CF Section
    put_ebcdic8(body, "CF01    "); // Name
    put_ebcdic8(body, sys_id);     // System
    for(int i=0; i<36; ++i) body.push_back(0); // skip to 52
    put_u32_be(body, 5000);        // total_req (off 52)
    for(int i=0; i<45; ++i) body.push_back(0); // skip to 101
    body.push_back(31);            // cf_level (off 101)
    for(int i=0; i<58; ++i) body.push_back(0); // pad to 160

    // Structure Section (2 entries)
    // Entry 1: ISGLOCK
    put_ebcdic8(body, "ISGLOCK ");
    put_ebcdic8(body, "        ");
    put_u32_be(body, 1024);        // size_4k (off 16)
    for(int i=0; i<12; ++i) body.push_back(0);

    // Entry 2: IXCSTR1
    put_ebcdic8(body, "IXCSTR1 ");
    put_ebcdic8(body, "        ");
    put_u32_be(body, 2048);        // size_4k (off 16)
    for(int i=0; i<12; ++i) body.push_back(0);

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf79_1_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 79, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1
    put_u32_be(body, 3); // TRN
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // [1] Monitor II Control
    put_triplet(body, current_off, 220, 2); // [2] AS State Data (2 entries)
    current_off += 220 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // AS State Data (2 entries)
    // Entry 1: CAT
    put_u16_be(body, 0x0001); // ASID
    put_ebcdic8(body, "CAT     "); // Job
    for(int i=0; i<22; ++i) body.push_back(0); // skip to 32
    put_u32_be(body, 10000);       // cpu_ms (off 32)
    for(int i=0; i<14; ++i) body.push_back(0); // skip to 50
    put_u32_be(body, 5000);        // real_frames (off 50)
    for(int i=0; i<44; ++i) body.push_back(0); // skip to 98
    put_ebcdic8(body, "SYSTEM  "); // service_class (off 98)
    for(int i=0; i<77; ++i) body.push_back(0); // skip to 183
    put_u32_be(body, 2000);        // ziip_ms (off 183)
    for(int i=0; i<33; ++i) body.push_back(0); // pad to 220

    // Entry 2: DOG
    put_u16_be(body, 0x0002); // ASID
    put_ebcdic8(body, "DOG     "); // Job
    for(int i=0; i<22; ++i) body.push_back(0);
    put_u32_be(body, 5000);        // cpu_ms
    for(int i=0; i<14; ++i) body.push_back(0);
    put_u32_be(body, 1000);        // real_frames
    for(int i=0; i<44; ++i) body.push_back(0);
    put_ebcdic8(body, "BATCH   "); // service_class
    for(int i=0; i<77; ++i) body.push_back(0);
    put_u32_be(body, 0);           // ziip_ms
    for(int i=0; i<33; ++i) body.push_back(0); // pad to 220

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf74_9_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 74, year, ddd, time_hs, sys_id);
    put_u16_be(body, 9); // subtype 9
    put_u32_be(body, 2); // TRN
    
    uint32_t current_off = 26 + 2 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, current_off, 184, 1); // [1] PCIE Function (1 entry)
    current_off += 184;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // PCIE Function Section (1 entry)
    put_u32_be(body, 0x00000001); // PFID
    for(int i=0; i<24; ++i) body.push_back(0); // skip to 28
    put_ebcdic8(body, "ZEDC    "); // job_name (off 28)
    put_u16_be(body, 0x0010);      // asid (off 36)
    for(int i=0; i<18; ++i) body.push_back(0); // skip to 56
    put_u64_be(body, 1000000);     // load_ops (off 56)
    put_u64_be(body, 500000);      // store_ops (off 64)
    for(int i=0; i<42; ++i) body.push_back(0); // skip to 114
    body.push_back(5);             // func_type (off 114)
    for(int i=0; i<69; ++i) body.push_back(0); // pad to 184

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf98_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 98, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1 (20-21)
    put_u16_be(body, 0); // Reserved (22-23)
    body.push_back(0);   // SMF98IND (24)
    body.push_back(0);   // SMF98PARTSEQNO (25)
    put_u16_be(body, 128); // SMF98SDSLEN (26-27)
    put_u16_be(body, 12);  // SMF98SDSTRIPLETSNUM (28-29)
    put_u16_be(body, 0);   // Reserved (30-31)
    
    uint32_t current_off = 32 + 12 * 8;
    for (int i = 0; i < 11; ++i) {
        put_u32_be(body, 0); put_u16_be(body, 0); put_u16_be(body, 0);
    }
    // [11] Consumption
    put_u32_be(body, current_off); put_u16_be(body, 32); put_u16_be(body, 2);
    current_off += 32 * 2;

    // Consumption Data (2 entries)
    // Entry 1: DB2
    put_u16_be(body, 0x0040);      // ASID
    put_ebcdic8(body, "DB2MSTR "); // Job
    for(int i=0; i<22; ++i) body.push_back(0);

    // Entry 2: CICS
    put_u16_be(body, 0x0050);      // ASID
    put_ebcdic8(body, "CICSREGN"); // Job
    for(int i=0; i<22; ++i) body.push_back(0);

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf74_5_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 74, year, ddd, time_hs, sys_id);
    put_u16_be(body, 5); // subtype 5
    put_u32_be(body, 3); // TRN
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, current_off, 32, 1); // [1] Control
    current_off += 32;
    put_triplet(body, current_off, 48, 2); // [2] Cache Device (2 entries)
    current_off += 48 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // Control Section
    for(int i=0; i<32; ++i) body.push_back(0);

    // Cache Device Section (2 entries)
    // Entry 1: VOL001
    put_ebcdic8(body, "VOL001  "); // Volser
    put_u16_be(body, 0x1001);      // device_num (off 10)
    for(int i=0; i<6; ++i) body.push_back(0); // skip to 18
    put_u32_be(body, 10000);       // read_req
    put_u32_be(body, 9000);        // read_hit
    put_u32_be(body, 5000);        // write_req
    put_u32_be(body, 4500);        // write_hit
    for(int i=0; i<14; ++i) body.push_back(0); // pad to 48

    // Entry 2: VOL002
    put_ebcdic8(body, "VOL002  "); // Volser
    put_u16_be(body, 0x1002);      // device_num
    for(int i=0; i<6; ++i) body.push_back(0);
    put_u32_be(body, 20000);       // read_req
    put_u32_be(body, 18000);       // read_hit
    put_u32_be(body, 10000);       // write_req
    put_u32_be(body, 9000);        // write_hit
    for(int i=0; i<14; ++i) body.push_back(0); // pad to 48

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf74_8_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 74, year, ddd, time_hs, sys_id);
    put_u16_be(body, 8); // subtype 8
    put_u32_be(body, 4); // TRN
    
    uint32_t current_off = 26 + 4 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // [1] Link Control
    put_triplet(body, 0, 0, 0); // [2] Link Data
    put_triplet(body, current_off, 24, 2); // [3] Extent Pool (2 entries)
    current_off += 24 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // Extent Pool Data (2 entries)
    // Entry 1: Pool 1
    put_u32_be(body, 0x00000001); // PID
    put_u32_be(body, 0x00000084); // PLT (CKD 1GB)
    for(int i=0; i<4; ++i) body.push_back(0);
    put_u32_be(body, 10000);       // cap_gb
    put_u32_be(body, 10000);       // extents
    put_u32_be(body, 5000);        // alloc

    // Entry 2: Pool 2
    put_u32_be(body, 0x00000002); // PID
    put_u32_be(body, 0x00000084); // PLT
    for(int i=0; i<4; ++i) body.push_back(0);
    put_u32_be(body, 20000);       // cap_gb
    put_u32_be(body, 20000);       // extents
    put_u32_be(body, 18000);       // alloc

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf70_2_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 70, year, ddd, time_hs, sys_id);
    put_u16_be(body, 2); // subtype 2
    put_u32_be(body, 2); // TRN
    
    uint32_t current_off = 26 + 2 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, current_off, 64, 2); // [1] CCA Coprocessor (2 entries)
    current_off += 64 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // CCA Coprocessor (2 entries)
    // Entry 1: Card 0
    put_u16_be(body, 0x0000); // index
    put_u16_be(body, 14);     // type (CEX8C)
    for(int i=0; i<12; ++i) body.push_back(0);
    put_u64_be(body, 0x4110000000000000); // exec_time (IBM float)
    put_u64_be(body, 0x4110000000000000); // ops_count

    // Entry 2: Card 1
    put_u16_be(body, 0x0001); // index
    put_u16_be(body, 14);     // type
    for(int i=0; i<12; ++i) body.push_back(0);
    put_u64_be(body, 0x4120000000000000); // exec_time
    put_u64_be(body, 0x4120000000000000); // ops_count

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf79_2_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 79, year, ddd, time_hs, sys_id);
    put_u16_be(body, 2); // subtype 2
    put_u32_be(body, 3); // TRN
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // [1] Monitor II Control
    put_triplet(body, current_off, 160, 2); // [2] AS Resource Data (2 entries)
    current_off += 160 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // AS Resource Data (2 entries)
    // Entry 1: CAT
    put_u16_be(body, 0x0001); // ASID
    put_ebcdic8(body, "CAT     "); // Job
    for(int i=0; i<22; ++i) body.push_back(0); // skip to 32
    put_u32_be(body, 15000);       // cpu_ms (off 32)
    for(int i=0; i<50; ++i) body.push_back(0); // skip to 86
    put_u32_be(body, 100);         // fixed_frames (off 86)
    for(int i=0; i<26; ++i) body.push_back(0); // skip to 116
    put_ebcdic8(body, "SYSTEM  "); // service_class (off 116)
    for(int i=0; i<36; ++i) body.push_back(0); // pad to 160

    // Entry 2: DOG
    put_u16_be(body, 0x0002); // ASID
    put_ebcdic8(body, "DOG     "); // Job
    for(int i=0; i<22; ++i) body.push_back(0);
    put_u32_be(body, 2000);        // cpu_ms
    for(int i=0; i<50; ++i) body.push_back(0);
    put_u32_be(body, 10);          // fixed_frames
    for(int i=0; i<26; ++i) body.push_back(0);
    put_ebcdic8(body, "BATCH   "); // service_class
    for(int i=0; i<36; ++i) body.push_back(0); // pad to 160

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf76_1_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 76, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1
    put_u32_be(body, 3); // TRN
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // [1] Control
    put_triplet(body, current_off, 24, 1); // [2] Paging Data
    current_off += 24;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // Paging Data Section
    put_u32_be(body, 50); // page_ins
    put_u32_be(body, 10); // page_outs
    put_u32_be(body, 5);  // swap_ins
    put_u32_be(body, 2);  // swap_outs
    put_u32_be(body, 100000); // frame_count
    put_u32_be(body, 80000);  // working_set

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf79_13_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 79, year, ddd, time_hs, sys_id);
    put_u16_be(body, 13); // subtype 13
    put_u32_be(body, 3);  // TRN
    
    uint32_t current_off = 26 + 3 * 12;
    put_triplet(body, current_off, 48, 1); // [0] Product
    current_off += 48;
    put_triplet(body, 0, 0, 0); // [1] Control
    put_triplet(body, current_off, 10, 2); // [2] Data (2 entries)
    current_off += 10 * 2;

    // Product Section
    body.push_back(0); // version
    for(int i=0; i<8; ++i) body.push_back(ebcdic_cp037("RMF"[i%3])); // product
    put_u32_be(body, time_hs);
    put_packed_yyyyddd(body, year, ddd);
    put_u32_be(body, 90000); // interval
    put_u16_be(body, 1000);  // sample count
    for(int i=0; i<26; ++i) body.push_back(0);

    // Event Data (2 entries)
    // Entry 1: LPAR Weight Change
    put_u16_be(body, 0x0001); // Event type
    put_ebcdic8(body, "WEIGHT  "); // Event data

    // Entry 2: CPU Add
    put_u16_be(body, 0x0002); // Event type
    put_ebcdic8(body, "CPUADD  "); // Event data

    return finish(std::move(body));
}

std::vector<uint8_t> make_smf1154_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 1154, year, ddd, time_hs, sys_id);
    
    // Header is already 32 bytes from put_header.
    // We need to inject Subtype at 22 and TRN at 24.
    body[22] = 0; body[23] = 1; // Subtype 1
    body[24] = 0; body[25] = 2; // TRN (2 triplets)
    
    uint32_t current_off = 32 + 2 * 12; // Triplets start at 32
    // We'll replace the reserved bytes at 32+ with triplets.
    // Since body is already 32 bytes, we can just append.
    
    std::vector<uint8_t> data;
    put_triplet(data, current_off, 64, 1); // [0] Common Header
    current_off += 64;
    put_triplet(data, current_off, 128, 1); // [1] Subtype Spec Section
    current_off += 128;

    // Common Header Section
    put_ebcdic8(data, sys_id);
    put_ebcdic8(data, "PLEX1   ");
    put_ebcdic8(data, "TCPIP   ");
    for(int i=0; i<8; ++i) data.push_back(0);
    put_ebcdic8(data, "REQ0001 "); // Request ID (24 bytes)
    for(int i=0; i<16; ++i) data.push_back(0);

    // Subtype Spec Section (contains its own triplets)
    uint32_t spec_base = current_off - 128;
    put_u32_be(data, 1); // 1 triplet
    uint32_t tcp_off = spec_base + 4 + 12;
    put_triplet(data, tcp_off, 16, 1); // [0] TCP Stack Info
    
    // TCP Stack Info
    put_ebcdic8(data, "TCPIP   "); // stack name
    data.push_back(4);            // ip version
    for(int i=0; i<3; ++i) data.push_back(0);
    put_u32_be(data, 3600);       // up time

    body.insert(body.end(), data.begin(), data.end());
    return finish(std::move(body));
}

std::vector<uint8_t> make_rmf_record(uint8_t type, uint16_t subtype, int year, int ddd, uint32_t time_hs,
                                     int n_triplets, int data_len, int n_entries = 1, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, type, year, ddd, time_hs, sys_id);
    put_u16_be(body, subtype);
    put_u32_be(body, static_cast<uint32_t>(n_triplets));   // SMF*TRN: number of TRIPLETS
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

std::vector<uint8_t> make_smf74_record(uint8_t type, uint16_t subtype, int year, int ddd, uint32_t time_hs,
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

std::vector<uint8_t> make_smf113_record(int year, int ddd, uint32_t time_hs, const char* sys_id = "SYS1") {
    std::vector<uint8_t> body;
    put_header(body, 113, year, ddd, time_hs, sys_id);
    put_u16_be(body, 1); // subtype 1

    // Triplets at offset 26 (4-2-2)
    // [0] Subsystem
    // [1] Identification
    // [2] Counter Data
    
    uint32_t current_off = 26 + 3 * 8;
    
    // Subsystem
    put_u32_be(body, 0); put_u16_be(body, 0); put_u16_be(body, 0);
    
    // Identification
    put_u32_be(body, current_off); put_u16_be(body, 32); put_u16_be(body, 1);
    current_off += 32;

    // Counter Data
    put_u32_be(body, current_off); put_u16_be(body, 8); put_u16_be(body, 4);
    current_off += 8 * 4;

    // Identification Section
    for(int i=0; i<14; ++i) body.push_back(0);
    put_u16_be(body, 0x0001); // CPU ID
    for(int i=0; i<12; ++i) body.push_back(0);
    body.push_back(0x00); // CP Class

    // Counter Data (4 counters)
    put_u64_be(body, 1000000000); // Cycles
    put_u64_be(body, 500000000);  // Instructions
    put_u64_be(body, 100000);     // L1 Miss Dir
    put_u64_be(body, 200000);     // L1 Miss Pen

    return finish(std::move(body));
}

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
                put_header(body, type, Y, ddd, t, sys);
                if (has_sub) put_u16_be(body, sub);
                // Pad to reasonable size
                for(int j=0; j<32; ++j) body.push_back(0x40);
                emit(finish(std::move(body)));
            };

            auto emit_rmf_small = [&](uint8_t type, uint16_t sub, int triplets, int len, int entries) {
                std::vector<uint8_t> body;
                put_header(body, type, Y, ddd, t, sys);
                put_u16_be(body, sub);
                put_u32_be(body, static_cast<uint32_t>(triplets));
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
            emit(make_smf70_2_record(Y, ddd, t, sys));
            emit(make_smf71_record(Y, ddd, t, sys));
            emit(make_smf72_3_record(Y, ddd, t, "BATCH", "PRODSTEP", 2, sys));
            emit_small(72, true, 1); // Other subtype (header only)
            emit(make_smf73_record(Y, ddd, t, sys));
            emit_rmf_small(74, 1, 3, 72, 2); // 2 devices
            emit(make_smf74_4_record(Y, ddd, t, sys));
            emit(make_smf74_5_record(Y, ddd, t, sys));
            emit(make_smf74_8_record(Y, ddd, t, sys));
            emit(make_smf74_9_record(Y, ddd, t, sys));
            emit_rmf_small(75, 1, 2, 80, 2); // 2 pagesets
            emit(make_smf76_1_record(Y, ddd, t, sys));
            emit(make_smf77_record(Y, ddd, t, sys));
            emit(make_smf78_3_record(Y, ddd, t, sys));
            emit(make_smf79_1_record(Y, ddd, t, sys));
            emit(make_smf79_2_record(Y, ddd, t, sys));
            emit(make_smf79_13_record(Y, ddd, t, sys));
            emit(make_smf98_record(Y, ddd, t, sys));
            emit(make_smf1154_record(Y, ddd, t, sys));
            emit_small(99, true, 1); // subtype 1
            emit_small(99, true, 2); // subtype 2
            emit(make_smf113_record(Y, ddd, t, sys));
            emit_small(113, true, 255); // Other subtype (header only)
            emit_small(250, false, 0);
        }
    } else if (profile == "large") {
        // 24 hours = 96 intervals of 15 mins
        const char* sys = "SYS1";
        for (int intv = 0; intv < 96; ++intv) {
            uint32_t t = intv * 15 * 60 * 100;
            
            // System-level CPU activity
            emit(make_smf70_record(Y, DAY, t, sys));
            emit(make_smf70_2_record(Y, DAY, t, sys)); // Crypto
            
            // Paging activity
            emit(make_smf71_record(Y, DAY, t, sys));
            emit(make_smf76_1_record(Y, DAY, t, sys)); // Paging (MII)

            // Channel path activity
            emit(make_smf73_record(Y, DAY, t, sys));

            // Workload activity: 3 service classes
            emit(make_smf72_3_record(Y, DAY, t, "SYSTEM", "SYSSTC", 1, sys));
            emit(make_smf72_3_record(Y, DAY, t, "BATCH",  "BATCHMED", 3, sys));
            emit(make_smf72_3_record(Y, DAY, t, "ONLINE", "CICSPOOL", 1, sys));

            // Coupling Facility & Storage Systems
            emit(make_smf74_4_record(Y, DAY, t, sys));
            emit(make_smf74_5_record(Y, DAY, t, sys));
            emit(make_smf74_8_record(Y, DAY, t, sys));
            emit(make_smf74_9_record(Y, DAY, t, sys));

            // Monitor II Address Space & Evaluation
            emit(make_smf79_1_record(Y, DAY, t, sys));
            emit(make_smf79_2_record(Y, DAY, t, sys));
            emit(make_smf79_13_record(Y, DAY, t, sys));

            // High-Frequency Throughput
            emit(make_smf98_record(Y, DAY, t, sys));

            // Interval records (Subtype 2) for long-running regions
            emit(make_smf30_record(2, Y, DAY, t, "CICSPROD", "CICSSTEP", 5000, 1200, sys));
            emit(make_smf30_record(2, Y, DAY, t, "DB2MSTR",  "DB2STEP",  2000, 500,  sys));
            emit(make_smf30_record(2, Y, DAY, t, "DB2DIST",  "DISTSTEP", 3000, 800,  sys));
            
            // Raw records for CICS (110) and DB2 (101)
            emit(make_record(110, false, 0, Y, DAY, t, sys));
            emit(make_record(101, false, 0, Y, DAY, t, sys));

            // Hardware counters
            emit(make_smf113_record(Y, DAY, t, sys));
            
            // Enqueue activity
            emit(make_smf77_record(Y, DAY, t, sys));

            // I/O Queuing activity
            emit(make_smf78_3_record(Y, DAY, t, sys));

            // Page data sets: 10 entries
            emit(make_rmf_record(75, 1, Y, DAY, t, 2, 80, 10, sys)); 

            // Device activity: 400 devices (fits in 32KB)
            emit(make_rmf_record(74, 1, Y, DAY, t, 3, 72, 400, sys));

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
