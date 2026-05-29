#pragma once
/*
 * smf_file_reader.h — RECFM=VB file-level driver with templated sink
 *
 * process_smf_file() handles the outer RECFM=VB record loop, reads the
 * standard 20-byte SMF header from each record, then hands control to the
 * sink via on_record(hdr, r).  The cursor r.pos is 20 on entry to on_record;
 * the subtype word (if any) and all section data are left for the sink or
 * record-type parser to consume.
 *
 * The Sink template parameter is inlined — no virtual dispatch, no copies.
 *
 * Example
 * ───────
 *   struct Dispatcher {
 *       Smf30Sink smf30;
 *
 *       void on_record(const mf::SmfHeader& hdr, mf::Reader& r) {
 *           switch (hdr.record_type) {
 *               case 30: mf::parse_smf30(hdr, r, smf30); break;
 *               default: break;
 *           }
 *       }
 *   };
 *
 *   // Memory-mapped file → span
 *   auto data = std::span<const std::byte>(mapped_ptr, file_size);
 *   Dispatcher sink;
 *   mf::process_smf_file(data, sink);
 */

#include "smf_sink.h"

namespace mf {

template<typename Sink>
    requires SmfRecordSink<Sink>
constexpr void process_smf_file(std::span<const std::byte> file_data, Sink& sink) {
    Reader file{file_data};
    while (file.can_read(4)) {
        const Rdw rdw = file.read_rdw();
        if (rdw.data_len == 0) break;
        if (!file.can_read(rdw.data_len)) break;

        Reader rec{file.data.subspan(file.pos, rdw.data_len)};
        file.skip(rdw.data_len);

        if (!rec.can_read(20)) continue;  /* skip malformed records */
        const SmfHeader hdr = read_smf_header(rec);
        sink.on_record(hdr, rec);
    }
}

} // namespace mf
