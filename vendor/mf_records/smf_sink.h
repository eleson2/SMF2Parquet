#pragma once
/*
 * smf_sink.h — Template sink concepts and built-in sink types
 *
 * The sink pattern connects parsers to output backends (Parquet, Arrow,
 * in-memory buffers, stdout) without an intermediate text step.
 * Every sink template parameter is fully inlined by the compiler — zero
 * virtual-dispatch overhead.
 *
 * Concepts
 * ────────
 *   SmfSink<S,R>      — S has write(const R&); used by per-record-type parsers.
 *   SmfRecordSink<S>  — S has on_record(const SmfHeader&, Reader&);
 *                       used by process_smf_file() to hand off each raw record.
 *
 * Writing a sink
 * ──────────────
 *   // Per-type sink (e.g. writes SMF30 rows to a Parquet file)
 *   struct MySmf30Sink {
 *       void write(const mf::Smf30Record& r) { ... }
 *   };
 *
 *   // File-level dispatcher (routes by record type)
 *   struct MyDispatcher {
 *       MySmf30Sink smf30;
 *       void on_record(const mf::SmfHeader& hdr, mf::Reader& r) {
 *           switch (hdr.record_type) {
 *               case 30: mf::parse_smf30(hdr, r, smf30); break;
 *               default: break;
 *           }
 *       }
 *   };
 *
 *   MyDispatcher sink;
 *   mf::process_smf_file(file_span, sink);
 */

#include "smf_reader.h"

namespace mf {

/* ── Concepts ─────────────────────────────────────────────────────────────── */

/* S is a per-record-type sink: must expose write(const R&) */
template<typename S, typename R>
concept SmfSink = requires(S& s, const R& rec) {
    s.write(rec);
};

/* S is a file-level sink: must expose on_record(const SmfHeader&, Reader&).
 * The cursor in r is at byte 20 (just past the SMF header) on entry. */
template<typename S>
concept SmfRecordSink = requires(S& s, const SmfHeader& hdr, Reader& r) {
    s.on_record(hdr, r);
};

/* ── Built-in sinks ───────────────────────────────────────────────────────── */

/* NullSink — discards every record.  Useful for benchmarking the parser alone
 * or for validation-only passes where output is not needed. */
struct NullSink {
    template<typename R>
    constexpr void write(const R&) noexcept {}

    constexpr void on_record(const SmfHeader&, Reader&) noexcept {}
};

} // namespace mf
