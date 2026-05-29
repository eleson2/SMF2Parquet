#pragma once
/*
 * smf/smf30_reader.h — SMF Type 30 record parser (parse core).
 *
 * SMF type 30 records are produced by z/OS for every job step and job that
 * executes on the system.  They are the primary source for chargeback,
 * capacity, and workload reporting.
 *
 * Record structure (z/OS 3.1, SA23-1370 / IBM SMF Explorer):
 *
 *   Offset 0-19   Standard 20-byte SMF header  (read via read_smf_header)
 *   Offset 20-21  Subtype (uint16 BE)
 *   Offset 22+    Section triplets (12 bytes each): offset, count, length
 *
 * Subtypes:
 *   1  Job/session initiation
 *   2  Interval record (long-running jobs)
 *   3  Step termination (most commonly queried)
 *   4  Job/session termination
 *   5  APPC/MVS
 *   6  OpenMVS/UNIX process
 *
 * Triplet order in record header (SA23-1370, z/OS 3.1):
 *   Index 0  Subsystem        (SMF30SOF/SLN/SON)  — not currently parsed
 *   Index 1  Identification   (SMF30IOF/ILN/ION)
 *   Index 2  I/O Activity     (SMF30UOF/ULN/UON)
 *   Index 3  Completion       (SMF30TOF/TLN/TON)  — elapsed + CPU time
 *   Index 4  CPU Accounting   (SMF30COF/CLN/CON)  — TCB / SRB / zIIP
 *   Index 5  Accounting       (SMF30AOF/ALN/AON)
 *   Index 6  Storage          (SMF30ROF/RLN/RON)
 *   Index 7  Performance      (SMF30POF/PLN/PON)  — service units, WLM
 *   ...
 *
 * IBM reference:
 *   https://ibm.github.io/IBM-SMF-Explorer/mappings/smf30/SMF30S3/
 *   z/OS MVS System Data Sets Reference  SA23-1370
 *   reference_doc/smf30_s3.md (local copy)
 *
 * TODO — Verify field offsets within sections against IBM SA23-1370 for
 *        your z/OS version before using in production.
 */

#include "../dataset_reader.h"
#include "../smf_reader.h"
#include "smf_section.h"

#include <cstdint>
#include <span>
#include <string>

namespace smf {

/* ── Result structures ──────────────────────────────────────────────────── */

struct Smf30Id {
    std::string job_name;     // SMFJBNAM  8 bytes EBCDIC  [IBM ref offset 0]
    std::string step_name;    // SMFSTSPN  8 bytes EBCDIC  [IBM ref offset 8]
    std::string job_id;       // SMFSTJNM  8 bytes EBCDIC  [IBM ref offset 16]
    std::string program_name; // SMFSTPRG  8 bytes EBCDIC  [IBM ref offset 24]
    // TODO: add user ID, account number, job class, …
};

struct Smf30Perf {
    // Completion section (SMF30TOF, triplet index 3)
    // TODO: verify field offsets within section against IBM SA23-1370
    uint32_t elapsed_time_hund{0};  // SMF30ETE: elapsed time (hundredths of seconds)
    uint32_t cpu_time_hund{0};      // SMF30CTE: total CPU time, TCB+SRB (hundredths)
};

struct Smf30Proc {
    // CPU Accounting section (SMF30COF, triplet index 4)
    // TODO: verify field offsets within section against IBM SA23-1370
    uint32_t tcb_time_hund{0};   // SMF30TCB: TCB CPU time (hundredths)
    uint32_t srb_time_hund{0};   // SMF30SRB: SRB CPU time (hundredths)
    uint32_t ziip_time_hund{0};  // SMF30ZIT: zIIP eligible time on zIIP (hundredths)
};

struct Smf30Io {
    // I/O Activity section (SMF30UOF, triplet index 2)
    // TODO: verify field offsets within section against IBM SA23-1370
    uint32_t excp_count{0};   // SMF30EXC: total I/O operations (EXCPs)
};

struct Smf30Record {
    mf::SmfHeader header;
    uint16_t      subtype{0};
    Smf30Id       id;
    Smf30Perf     perf;
    Smf30Proc     proc;
    Smf30Io       io;
};

// SectionPtr, read_section_ptr, make_section_reader — see smf_section.h

/* ── Section parsers ────────────────────────────────────────────────────── */

[[nodiscard]] inline Smf30Id read_smf30_id(mf::Reader& sr) {
    Smf30Id id;
    // [IBM ref] Identification section field offsets (from section entry start):
    id.job_name     = mf::rtrim(sr.read_ebcdic(8));  // offset 0
    id.step_name    = mf::rtrim(sr.read_ebcdic(8));  // offset 8
    id.job_id       = mf::rtrim(sr.read_ebcdic(8));  // offset 16
    id.program_name = mf::rtrim(sr.read_ebcdic(8));  // offset 24
    // TODO: verify offsets and add further fields (user ID, account, class, …)
    return id;
}

[[nodiscard]] inline Smf30Io read_smf30_io(mf::Reader& sr) {
    Smf30Io io;
    // TODO: verify field offsets against IBM SA23-1370
    io.excp_count = sr.read_u32();  // SMF30EXC at section offset 0
    return io;
}

[[nodiscard]] inline Smf30Perf read_smf30_perf(mf::Reader& sr) {
    Smf30Perf p;
    // TODO: verify field offsets against IBM SA23-1370
    p.elapsed_time_hund = sr.read_u32();  // SMF30ETE at section offset 0
    p.cpu_time_hund     = sr.read_u32();  // SMF30CTE at section offset 4
    return p;
}

[[nodiscard]] inline Smf30Proc read_smf30_proc(mf::Reader& sr) {
    Smf30Proc p;
    // TODO: verify field offsets against IBM SA23-1370
    p.tcb_time_hund  = sr.read_u32();  // SMF30TCB at section offset 0
    p.srb_time_hund  = sr.read_u32();  // SMF30SRB at section offset 4
    p.ziip_time_hund = sr.read_u32();  // SMF30ZIT at section offset 8
    return p;
}

/* ── Main SMF30 record parser ───────────────────────────────────────────── */

// Parses an SMF type 30 record from a raw record body span (after the RDW).
// rec_bytes[0] is the first byte of the SMF record (record_len high byte).
[[nodiscard]] inline Smf30Record read_smf30(std::span<const std::byte> rec_bytes) {
    mf::Reader r{rec_bytes};
    Smf30Record out;

    out.header  = mf::read_smf_header(r);  // advances to byte 20
    if (r.can_read(2)) out.subtype = r.read_u16();  // bytes 20-21 (absent on header-only records)

    // Section triplets from byte 22, in IBM-defined order (SA23-1370, z/OS 3.1).
    // Each triplet: uint32 offset, uint32 count, uint32 length (12 bytes total).
    // count == 0 means the section is absent for this record.
    [[maybe_unused]] const SectionPtr subsys_ptr = read_section_ptr(r);  // index 0: Subsystem (not parsed)
    const SectionPtr id_ptr   = read_section_ptr(r);  // index 1: Identification (SMF30IOF)
    const SectionPtr io_ptr   = read_section_ptr(r);  // index 2: I/O Activity   (SMF30UOF)
    const SectionPtr perf_ptr = read_section_ptr(r);  // index 3: Completion     (SMF30TOF)
    const SectionPtr proc_ptr = read_section_ptr(r);  // index 4: CPU Accounting (SMF30COF)

    mf::Reader sr{rec_bytes};  // reused sub-reader; make_section_reader resets it

    if (make_section_reader(rec_bytes, id_ptr,   32, sr)) out.id   = read_smf30_id  (sr);
    if (make_section_reader(rec_bytes, io_ptr,    4, sr)) out.io   = read_smf30_io  (sr);
    if (make_section_reader(rec_bytes, perf_ptr,  8, sr)) out.perf = read_smf30_perf(sr);
    if (make_section_reader(rec_bytes, proc_ptr, 12, sr)) out.proc = read_smf30_proc(sr);

    return out;
}

} // namespace smf
