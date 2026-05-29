#pragma once
/*
 * dataset_reader.h - Cursor-based reader for Mainframe record buffers
 *
 * Provides mf::Reader: a constexpr cursor class over a std::span<const std::byte>.
 * Every read method advances the internal position and returns the decoded value.
 * Non-advancing offset-based reads (peek_*) are also provided for triplet/section
 * patterns where a header supplies explicit byte offsets.
 *
 * Typical usage:
 *
 *   constexpr std::array<std::byte, 6> buf = { ... };
 *   mf::Reader r{buf};
 *   auto id  = r.read_i32();
 *   auto amt = r.read_packed(4, 2);
 *
 * For RECFM=VB files:
 *
 *   while (r.can_read(4)) {
 *       auto rdw = r.read_rdw();
 *       if (rdw.data_len == 0) break;
 *       mf::Reader rec{ r.data.subspan(r.pos, rdw.data_len) };
 *       r.skip(rdw.data_len);
 *       // process rec ...
 *   }
 */

#include "mf_types.h"
#include "codepages.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mf {

/* ── RDW ─────────────────────────────────────────────────────────────────── */

struct Rdw {
    uint16_t data_len; /* record data bytes following the 4-byte RDW header */
    uint8_t  flag1;    /* segment control byte 1 (0x00 for unsegmented)     */
    uint8_t  flag2;    /* segment control byte 2                            */
};

/* ── Reader ──────────────────────────────────────────────────────────────── */

class Reader {
public:
    std::span<const std::byte> data;
    std::size_t pos{0};

    constexpr explicit Reader(std::span<const std::byte> buf) noexcept : data(buf) {}

    /* ── Cursor ───────────────────────────────────────────────────────────── */

    [[nodiscard]] constexpr bool        can_read(std::size_t n)   const noexcept { return pos + n <= data.size(); }
    [[nodiscard]] constexpr std::size_t remaining()               const noexcept { return pos <= data.size() ? data.size() - pos : 0; }
    constexpr void skip(std::size_t n) noexcept { pos += n; }
    constexpr void seek(std::size_t p) noexcept { pos  = p; }

    /* ── Non-advancing peek / flag tests ─────────────────────────────────── */

    [[nodiscard]] constexpr std::byte peek()                       const noexcept { return data[pos]; }
    [[nodiscard]] constexpr uint8_t   peek_u8()                    const noexcept { return u8at(pos); }

    /* IBM bit numbering: bit 0 = MSB (0x80), bit 7 = LSB (0x01) */
    [[nodiscard]] constexpr bool test_bit (int ibm_bit)            const noexcept { return u8at(pos) & (0x80u >> ibm_bit); }
    [[nodiscard]] constexpr bool test_mask(uint8_t mask)           const noexcept { return u8at(pos) & mask; }

    /* ── Unsigned integers — big-endian ──────────────────────────────────── */

    [[nodiscard]] constexpr uint8_t  read_u8()  { assert(can_read(1)); const auto v = u8at(pos);    pos += 1; return v; }
    [[nodiscard]] constexpr uint16_t read_u16() { assert(can_read(2)); const auto v = be_u16(pos);  pos += 2; return v; }
    [[nodiscard]] constexpr uint32_t read_u32() { assert(can_read(4)); const auto v = be_u32(pos);  pos += 4; return v; }
    [[nodiscard]] constexpr uint64_t read_u64() { assert(can_read(8)); const auto v = be_u64(pos);  pos += 8; return v; }

    /* ── Signed integers — big-endian ────────────────────────────────────── */

    [[nodiscard]] constexpr int8_t   read_i8()  { return static_cast<int8_t> (read_u8());  }
    [[nodiscard]] constexpr int16_t  read_i16() { return static_cast<int16_t>(read_u16()); }
    [[nodiscard]] constexpr int32_t  read_i32() { return static_cast<int32_t>(read_u32()); }
    [[nodiscard]] constexpr int64_t  read_i64() { return static_cast<int64_t>(read_u64()); }

    /* ── Packed decimal (COMP-3) ─────────────────────────────────────────── */

    [[nodiscard]] constexpr int64_t read_packed_int(int len) {
        assert(can_read(static_cast<std::size_t>(len)));
        const auto v = packed_to_int64(data.subspan(pos, static_cast<std::size_t>(len)));
        pos += static_cast<std::size_t>(len);
        return v;
    }

    [[nodiscard]] constexpr double read_packed(int len, int scale) {
        assert(can_read(static_cast<std::size_t>(len)));
        const auto v = packed_to_dbl(data.subspan(pos, static_cast<std::size_t>(len)), scale);
        pos += static_cast<std::size_t>(len);
        return v;
    }

    /* ── Display numeric (PIC 9(n)) ──────────────────────────────────────── */

    [[nodiscard]] constexpr int64_t read_display_int(int len) {
        assert(can_read(static_cast<std::size_t>(len)));
        const auto v = display_to_int64(data.subspan(pos, static_cast<std::size_t>(len)));
        pos += static_cast<std::size_t>(len);
        return v;
    }

    [[nodiscard]] constexpr double read_display(int len, int scale) {
        assert(can_read(static_cast<std::size_t>(len)));
        const auto v = display_to_dbl(data.subspan(pos, static_cast<std::size_t>(len)), scale);
        pos += static_cast<std::size_t>(len);
        return v;
    }

    /* ── IBM hexadecimal float ───────────────────────────────────────────── */

    /* COMP-1: 4-byte IBM short float */
    [[nodiscard]] constexpr double read_comp1() { return ibm_float_to_ieee(read_u32()); }

    /* COMP-2: 8-byte IBM long float */
    [[nodiscard]] constexpr double read_comp2() { return ibm_float_to_ieee(read_u64()); }

    /* ── Strings ─────────────────────────────────────────────────────────── */

    /* PIC X — raw bytes as-is, null-terminated std::string */
    [[nodiscard]] constexpr std::string read_string(std::size_t len) {
        assert(can_read(len));
        std::string s(len, '\0');
        for (std::size_t i = 0; i < len; ++i)
            s[i] = static_cast<char>(u8at(pos + i));
        pos += len;
        return s;
    }

    /* PIC X — EBCDIC to ASCII, default CP037 */
    [[nodiscard]] constexpr std::string read_ebcdic(std::size_t len,
                                                    EbcdicCodepage cp = EbcdicCodepage::CP037) {
        assert(can_read(len));
        const auto& table = get_ebcdic_table(cp);
        std::string s(len, '\0');
        for (std::size_t i = 0; i < len; ++i)
            s[i] = static_cast<char>(table[u8at(pos + i)]);
        pos += len;
        return s;
    }

    /* PIC X — EBCDIC to ASCII, trimmed: strips trailing spaces (mainframe pad character).
     * Equivalent to rtrim(read_ebcdic(len)) but returns std::string directly. */
    [[nodiscard]] constexpr std::string read_ebcdic_trimmed(std::size_t len,
                                                            EbcdicCodepage cp = EbcdicCodepage::CP037) {
        return std::string(rtrim(read_ebcdic(len, cp)));
    }

    /* PIC X — EBCDIC to UTF-8; expands Latin-1 chars > 0x7F to 2-byte sequences */
    [[nodiscard]] constexpr std::string read_ebcdic_utf8(std::size_t len,
                                                         EbcdicCodepage cp = EbcdicCodepage::CP037) {
        assert(can_read(len));
        const auto& table = get_ebcdic_table(cp);
        std::string s;
        s.reserve(len * 2);
        for (std::size_t i = 0; i < len; ++i) {
            const uint8_t c = table[u8at(pos + i)];
            if (c < 0x80) {
                s.push_back(static_cast<char>(c));
            } else {
                s.push_back(static_cast<char>(0xC0 | (c >> 6)));
                s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        pos += len;
        return s;
    }

    /* ── RDW (RECFM=VB) ─────────────────────────────────────────────────── */

    [[nodiscard]] constexpr Rdw read_rdw() {
        assert(can_read(4));
        const uint16_t total = be_u16(pos);
        Rdw rdw;
        rdw.data_len = (total >= 4) ? static_cast<uint16_t>(total - 4) : uint16_t{0};
        rdw.flag1    = u8at(pos + 2);
        rdw.flag2    = u8at(pos + 3);
        pos += 4;
        return rdw;
    }

    /* ── Mainframe time / date / TOD clock ──────────────────────────────── */

    /* Read 4-byte uint32 hundredths-of-seconds time field */
    [[nodiscard]] constexpr MfTime   read_mf_time() { return decode_mf_time(read_u32()); }

    /* Read 4-byte COMP-3 YYYYDDD Julian date field */
    [[nodiscard]] constexpr MfDate   read_mf_date() { return decode_mf_date(read_packed_int(4)); }

    /* Read 8-byte IBM TOD clock value (decode with tod_to_unix_sec / tod_usec) */
    [[nodiscard]] constexpr uint64_t read_tod()     { return read_u64(); }

    /* ── Bit / flag helpers ───────────────────────────────────────────────── */

    /* Read a flag byte and advance; semantically distinct from read_u8 */
    [[nodiscard]] constexpr uint8_t read_flag() { return read_u8(); }

    /* ── Non-advancing reads (peek_*) ───────────────────────────────────── */
    /* Read at an explicit byte offset without moving pos.
     * Use when a record header supplies absolute section offsets (triplets)
     * or when you need to inspect a field without consuming it. */

    [[nodiscard]] constexpr uint8_t  peek_u8 (std::size_t off) const noexcept { assert(off + 1 <= data.size()); return u8at(off); }
    [[nodiscard]] constexpr uint16_t peek_u16(std::size_t off) const noexcept { assert(off + 2 <= data.size()); return be_u16(off); }
    [[nodiscard]] constexpr int16_t  peek_i16(std::size_t off) const noexcept { assert(off + 2 <= data.size()); return static_cast<int16_t>(be_u16(off)); }
    [[nodiscard]] constexpr uint32_t peek_u32(std::size_t off) const noexcept { assert(off + 4 <= data.size()); return be_u32(off); }
    [[nodiscard]] constexpr int32_t  peek_i32(std::size_t off) const noexcept { assert(off + 4 <= data.size()); return static_cast<int32_t>(be_u32(off)); }
    [[nodiscard]] constexpr uint64_t peek_u64(std::size_t off) const noexcept { assert(off + 8 <= data.size()); return be_u64(off); }
    [[nodiscard]] constexpr int64_t  peek_i64(std::size_t off) const noexcept { assert(off + 8 <= data.size()); return static_cast<int64_t>(be_u64(off)); }

    /* Mainframe time/date — non-advancing */
    [[nodiscard]] constexpr MfTime peek_mf_time(std::size_t off) const noexcept {
        assert(off + 4 <= data.size());
        return decode_mf_time(be_u32(off));
    }
    [[nodiscard]] constexpr MfDate peek_mf_date(std::size_t off) const noexcept {
        assert(off + 4 <= data.size());
        return decode_mf_date(packed_to_int64(data.subspan(off, 4)));
    }

    /* IBM hexadecimal float — non-advancing (COMP-1: 4 bytes, COMP-2: 8 bytes) */
    [[nodiscard]] constexpr double peek_comp1(std::size_t off) const noexcept {
        assert(off + 4 <= data.size());
        return ibm_float_to_ieee(be_u32(off));
    }
    [[nodiscard]] constexpr double peek_comp2(std::size_t off) const noexcept {
        assert(off + 8 <= data.size());
        return ibm_float_to_ieee(be_u64(off));
    }

    /* ── Factory methods — construct from raw pointer + length ───────────── */
    /* For runtime use only; reinterpret_cast precludes constant-expression.  */

    [[nodiscard]] static Reader from_bytes(const uint8_t* p, std::size_t len) noexcept {
        return Reader{ std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), len) };
    }
    [[nodiscard]] static Reader from_void(const void* p, std::size_t len) noexcept {
        return Reader{ std::span<const std::byte>(static_cast<const std::byte*>(p), len) };
    }

private:
    [[nodiscard]] constexpr uint8_t u8at(std::size_t i) const noexcept {
        return std::to_integer<uint8_t>(data[i]);
    }
    [[nodiscard]] constexpr uint16_t be_u16(std::size_t i) const noexcept { return be_to_u16(data.data() + i); }
    [[nodiscard]] constexpr uint32_t be_u32(std::size_t i) const noexcept { return be_to_u32(data.data() + i); }
    [[nodiscard]] constexpr uint64_t be_u64(std::size_t i) const noexcept { return be_to_u64(data.data() + i); }
};

} // namespace mf
