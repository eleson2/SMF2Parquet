#pragma once
/*
 * mf_types.h - Mainframe Type Conversions for C++
 *
 * Constexpr conversion primitives for mainframe data encodings.
 * Used as the support layer under dataset_reader.h.
 *
 * All functions are constexpr — usable in both compile-time and
 * runtime contexts.  Functions that read/write buffers accept
 * std::span<const std::byte> or std::byte* so they compose
 * naturally with the Reader cursor class.
 *
 * Not constexpr (use system calls): get_date_yyyymmdd, get_timestamp.
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <span>
#include <string>
#include <string_view>

namespace mf {

/*════════════════════════════════════════════════════════════════
  String Operations  (COBOL MOVE equivalents)
  All constexpr — operate on char arrays / string_view.
════════════════════════════════════════════════════════════════*/

/* MOVE alphanumeric — left justified, space padded */
constexpr void move_str(const char* src, std::size_t src_len,
                        char* dst, std::size_t dst_len) noexcept {
    const std::size_t n = std::min(src_len, dst_len);
    std::copy(src, src + n, dst);
    std::fill(dst + n, dst + dst_len, ' ');
}

/* MOVE from null-terminated C string */
constexpr void move_cstr(const char* src, char* dst, std::size_t dst_len) noexcept {
    move_str(src, std::char_traits<char>::length(src), dst, dst_len);
}

/* MOVE numeric — right justified, zero padded */
constexpr void move_num(const char* src, std::size_t src_len,
                        char* dst, std::size_t dst_len) noexcept {
    std::size_t start = 0;
    while (start < src_len && (src[start] == '0' || src[start] == ' ')) ++start;
    const std::size_t sig = src_len - start;
    if (sig >= dst_len) {
        std::copy(src + src_len - dst_len, src + src_len, dst);
    } else {
        const std::size_t pad = dst_len - sig;
        std::fill(dst, dst + pad, '0');
        std::copy(src + start, src + src_len, dst + pad);
    }
}

constexpr void move_spaces    (char* dst, std::size_t len) noexcept { std::fill(dst, dst + len, ' ');    }
constexpr void move_zeros     (char* dst, std::size_t len) noexcept { std::fill(dst, dst + len, '0');    }
constexpr void move_high_values(char* dst, std::size_t len) noexcept { std::fill(dst, dst + len, '\xFF'); }
constexpr void move_low_values (char* dst, std::size_t len) noexcept { std::fill(dst, dst + len, '\x00'); }

/*════════════════════════════════════════════════════════════════
  Comparison Operations
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr int compare_field(const char* a, std::size_t a_len,
                                          const char* b, std::size_t b_len) noexcept {
    const std::size_t min_len = std::min(a_len, b_len);
    for (std::size_t i = 0; i < min_len; ++i) {
        const auto ua = static_cast<unsigned char>(a[i]);
        const auto ub = static_cast<unsigned char>(b[i]);
        if (ua != ub) return ua < ub ? -1 : 1;
    }
    for (std::size_t i = min_len; i < a_len; ++i) if (a[i] != ' ') return  1;
    for (std::size_t i = min_len; i < b_len; ++i) if (b[i] != ' ') return -1;
    return 0;
}

[[nodiscard]] constexpr bool is_spaces     (const char* f, std::size_t n) noexcept { for (std::size_t i=0;i<n;++i) if (f[i]!=' ')    return false; return true; }
[[nodiscard]] constexpr bool is_zeros      (const char* f, std::size_t n) noexcept { for (std::size_t i=0;i<n;++i) if (f[i]!='0'&&f[i]!=0) return false; return true; }
[[nodiscard]] constexpr bool is_low_values (const char* f, std::size_t n) noexcept { for (std::size_t i=0;i<n;++i) if (f[i]!=0)     return false; return true; }
[[nodiscard]] constexpr bool is_high_values(const char* f, std::size_t n) noexcept { for (std::size_t i=0;i<n;++i) if (static_cast<unsigned char>(f[i])!=0xFF) return false; return true; }

[[nodiscard]] constexpr bool is_numeric(const char* field, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        const char c = field[i];
        if (c >= '0' && c <= '9') continue;
        if (i == len-1 && ((c>='A'&&c<='I')||(c>='J'&&c<='R')||c=='{'||c=='}')) continue;
        if (i == 0   && (c == '+' || c == '-')) continue;
        return false;
    }
    return true;
}

/*════════════════════════════════════════════════════════════════
  Packed Decimal (COMP-3)
  Buffer arguments use std::span<const std::byte> for constexpr safety.
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr int64_t packed_to_int64(std::span<const std::byte> p) noexcept {
    const int len = static_cast<int>(p.size());
    int64_t result = 0;
    for (int i = 0; i < len - 1; ++i) {
        const auto b = std::to_integer<uint8_t>(p[i]);
        result = result * 10 + (b >> 4);
        result = result * 10 + (b & 0x0F);
    }
    const auto last = std::to_integer<uint8_t>(p[len - 1]);
    result = result * 10 + (last >> 4);
    const int sign = last & 0x0F;
    return (sign == 0x0D || sign == 0x0B) ? -result : result;
}

[[nodiscard]] constexpr double packed_to_dbl(std::span<const std::byte> p, int scale) noexcept {
    double divisor = 1.0;
    for (int i = 0; i < scale; ++i) divisor *= 10.0;
    return static_cast<double>(packed_to_int64(p)) / divisor;
}

constexpr void int64_to_packed(int64_t val, std::byte* p, int len) noexcept {
    const int sign = (val < 0) ? 0x0D : 0x0C;
    if (val < 0) val = -val;
    std::fill(p, p + len, std::byte{0});
    p[len-1] = std::byte{static_cast<uint8_t>(((val % 10) << 4) | sign)};
    val /= 10;
    for (int i = len - 2; i >= 0 && val > 0; --i) {
        const uint8_t lo = static_cast<uint8_t>(val % 10); val /= 10;
        const uint8_t hi = static_cast<uint8_t>(val % 10); val /= 10;
        p[i] = std::byte{static_cast<uint8_t>((hi << 4) | lo)};
    }
}

constexpr void dbl_to_packed(double val, std::byte* p, int len, int scale) noexcept {
    double m = 1.0;
    for (int i = 0; i < scale; ++i) m *= 10.0;
    int64_to_packed(static_cast<int64_t>(val * m + (val >= 0 ? 0.5 : -0.5)), p, len);
}

constexpr void packed_add(std::span<const std::byte> a, std::span<const std::byte> b,
                          std::byte* result, int len) noexcept {
    int64_to_packed(packed_to_int64(a) + packed_to_int64(b), result, len);
}
constexpr void packed_sub(std::span<const std::byte> a, std::span<const std::byte> b,
                          std::byte* result, int len) noexcept {
    int64_to_packed(packed_to_int64(a) - packed_to_int64(b), result, len);
}
constexpr void packed_mul(std::span<const std::byte> a, std::span<const std::byte> b,
                          std::byte* result, int len) noexcept {
    int64_to_packed(packed_to_int64(a) * packed_to_int64(b), result, len);
}
constexpr void packed_div(std::span<const std::byte> a, std::span<const std::byte> b,
                          std::byte* result, int len) noexcept {
    const int64_t vb = packed_to_int64(b);
    if (vb != 0) {
        int64_to_packed(packed_to_int64(a) / vb, result, len);
    } else {
        /* Division by zero — fill with 9s */
        std::fill(result, result + len - 1, std::byte{0x99});
        result[len - 1] = std::byte{0x9C};
    }
}

/*════════════════════════════════════════════════════════════════
  Display Numeric  (PIC 9(n) / PIC S9(n))
  Handles zoned decimal and trailing overpunch signs (ASCII).
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr int64_t display_to_int64(std::span<const std::byte> p) noexcept {
    const int len = static_cast<int>(p.size());
    int64_t result = 0;
    bool neg = false;
    for (int i = 0; i < len; ++i) {
        char c = static_cast<char>(std::to_integer<uint8_t>(p[i]));
        if (i == len - 1) {
            if      (c >= 'J' && c <= 'R') { neg = true;  c = static_cast<char>('0' + (c - 'J' + 1)); }
            else if (c == '}')             { neg = true;  c = '0'; }
            else if (c >= 'A' && c <= 'I') {              c = static_cast<char>('0' + (c - 'A' + 1)); }
            else if (c == '{')             {              c = '0'; }
        }
        if (c >= '0' && c <= '9') result = result * 10 + (c - '0');
    }
    return neg ? -result : result;
}

[[nodiscard]] constexpr double display_to_dbl(std::span<const std::byte> p, int scale) noexcept {
    double divisor = 1.0;
    for (int i = 0; i < scale; ++i) divisor *= 10.0;
    return static_cast<double>(display_to_int64(p)) / divisor;
}

constexpr void int64_to_display(int64_t val, char* p, int len) noexcept {
    const bool neg = val < 0;
    if (neg) val = -val;
    std::fill(p, p + len, '0');
    for (int i = len - 1; i >= 0 && val > 0; --i) {
        p[i] = static_cast<char>('0' + val % 10);
        val /= 10;
    }
    if (neg) {
        p[len-1] = (p[len-1] == '0') ? '}' : static_cast<char>('J' + (p[len-1] - '1'));
    }
}

constexpr void dbl_to_display(double val, char* p, int len, int scale) noexcept {
    double m = 1.0;
    for (int i = 0; i < scale; ++i) m *= 10.0;
    int64_to_display(static_cast<int64_t>(val * m + (val >= 0 ? 0.5 : -0.5)), p, len);
}

/*════════════════════════════════════════════════════════════════
  Binary (COMP / COMP-5)  — big-endian <-> native
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr uint16_t be_to_u16(const std::byte* p) noexcept {
    return (uint16_t{std::to_integer<uint8_t>(p[0])} << 8) |
            uint16_t{std::to_integer<uint8_t>(p[1])};
}
[[nodiscard]] constexpr int16_t  be_to_i16(const std::byte* p) noexcept { return static_cast<int16_t> (be_to_u16(p)); }

[[nodiscard]] constexpr uint32_t be_to_u32(const std::byte* p) noexcept {
    return (uint32_t{std::to_integer<uint8_t>(p[0])} << 24) |
           (uint32_t{std::to_integer<uint8_t>(p[1])} << 16) |
           (uint32_t{std::to_integer<uint8_t>(p[2])} <<  8) |
            uint32_t{std::to_integer<uint8_t>(p[3])};
}
[[nodiscard]] constexpr int32_t  be_to_i32(const std::byte* p) noexcept { return static_cast<int32_t> (be_to_u32(p)); }

[[nodiscard]] constexpr uint64_t be_to_u64(const std::byte* p) noexcept {
    return (uint64_t{std::to_integer<uint8_t>(p[0])} << 56) |
           (uint64_t{std::to_integer<uint8_t>(p[1])} << 48) |
           (uint64_t{std::to_integer<uint8_t>(p[2])} << 40) |
           (uint64_t{std::to_integer<uint8_t>(p[3])} << 32) |
           (uint64_t{std::to_integer<uint8_t>(p[4])} << 24) |
           (uint64_t{std::to_integer<uint8_t>(p[5])} << 16) |
           (uint64_t{std::to_integer<uint8_t>(p[6])} <<  8) |
            uint64_t{std::to_integer<uint8_t>(p[7])};
}
[[nodiscard]] constexpr int64_t  be_to_i64(const std::byte* p) noexcept { return static_cast<int64_t> (be_to_u64(p)); }

constexpr void u16_to_be(uint16_t v, std::byte* p) noexcept {
    p[0] = std::byte{static_cast<uint8_t>(v >> 8)};
    p[1] = std::byte{static_cast<uint8_t>(v)};
}
constexpr void i16_to_be(int16_t  v, std::byte* p) noexcept { u16_to_be(static_cast<uint16_t>(v), p); }

constexpr void u32_to_be(uint32_t v, std::byte* p) noexcept {
    p[0] = std::byte{static_cast<uint8_t>(v >> 24)};
    p[1] = std::byte{static_cast<uint8_t>(v >> 16)};
    p[2] = std::byte{static_cast<uint8_t>(v >>  8)};
    p[3] = std::byte{static_cast<uint8_t>(v)};
}
constexpr void i32_to_be(int32_t  v, std::byte* p) noexcept { u32_to_be(static_cast<uint32_t>(v), p); }

constexpr void u64_to_be(uint64_t v, std::byte* p) noexcept {
    p[0] = std::byte{static_cast<uint8_t>(v >> 56)};
    p[1] = std::byte{static_cast<uint8_t>(v >> 48)};
    p[2] = std::byte{static_cast<uint8_t>(v >> 40)};
    p[3] = std::byte{static_cast<uint8_t>(v >> 32)};
    p[4] = std::byte{static_cast<uint8_t>(v >> 24)};
    p[5] = std::byte{static_cast<uint8_t>(v >> 16)};
    p[6] = std::byte{static_cast<uint8_t>(v >>  8)};
    p[7] = std::byte{static_cast<uint8_t>(v)};
}
constexpr void i64_to_be(int64_t  v, std::byte* p) noexcept { u64_to_be(static_cast<uint64_t>(v), p); }

/*════════════════════════════════════════════════════════════════
  IBM Hexadecimal Float (COMP-1 / COMP-2)
  Overloaded on uint32_t (COMP-1) and uint64_t (COMP-2).
  Structure: 1-bit sign | 7-bit exponent (base-16, bias 64) | fraction
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr double ibm_float_to_ieee(uint32_t bits) noexcept {
    const int      sign     = static_cast<int>(bits >> 31) & 1;
    const int      exponent = static_cast<int>(bits >> 24) & 0x7F;
    const uint32_t mantissa = bits & 0x00FFFFFF;
    if (mantissa == 0) return 0.0;
    double val = static_cast<double>(mantissa) / 16777216.0; /* 2^24 */
    const int power = exponent - 64;
    if (power > 0) for (int i = 0; i < power; ++i) val *= 16.0;
    else           for (int i = 0; i > power; --i) val /= 16.0;
    return sign ? -val : val;
}

[[nodiscard]] constexpr double ibm_float_to_ieee(uint64_t bits) noexcept {
    const int      sign     = static_cast<int>(bits >> 63) & 1;
    const int      exponent = static_cast<int>(bits >> 56) & 0x7F;
    const uint64_t mantissa = bits & 0x00FFFFFFFFFFFFFFULL;
    if (mantissa == 0) return 0.0;
    double val = static_cast<double>(mantissa) / 72057594037927936.0; /* 2^56 */
    const int power = exponent - 64;
    if (power > 0) for (int i = 0; i < power; ++i) val *= 16.0;
    else           for (int i = 0; i > power; --i) val /= 16.0;
    return sign ? -val : val;
}

/*════════════════════════════════════════════════════════════════
  EBCDIC / ASCII Conversion
  Generic mapping for COBOL field operations.
  For record field reading with codepage accuracy use
  mf::Reader::read_ebcdic() which uses codepages.h tables.
════════════════════════════════════════════════════════════════*/

inline constexpr std::array<uint8_t, 256> mf_ebcdic_to_ascii = {{
    0x00,0x01,0x02,0x03,0x9C,0x09,0x86,0x7F,0x97,0x8D,0x8E,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x9D,0x85,0x08,0x87,0x18,0x19,0x92,0x8F,0x1C,0x1D,0x1E,0x1F,
    0x80,0x81,0x82,0x83,0x84,0x0A,0x17,0x1B,0x88,0x89,0x8A,0x8B,0x8C,0x05,0x06,0x07,
    0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9A,0x9B,0x14,0x15,0x9E,0x1A,
    0x20,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xD5,0x2E,0x3C,0x28,0x2B,0x7C,
    0x26,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0x21,0x24,0x2A,0x29,0x3B,0xAC,
    0x2D,0x2F,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xE5,0x2C,0x25,0x5F,0x3E,0x3F,
    0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,0xC0,0xC1,0xC2,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
    0xC3,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,
    0xCA,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,
    0xD1,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xD2,0xD3,0xD4,0x5B,0xD6,0xD7,
    0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,0xE0,0xE1,0xE2,0xE3,0xE4,0x5D,0xE6,0xE7,
    0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,
    0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,
    0x5C,0x9F,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
}};

inline constexpr std::array<uint8_t, 256> mf_ascii_to_ebcdic = {{
    0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
    0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
    0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
    0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x5F,0x6D,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
    0x20,0x21,0x22,0x23,0x24,0x15,0x06,0x17,0x28,0x29,0x2A,0x2B,0x2C,0x09,0x0A,0x1B,
    0x30,0x31,0x1A,0x33,0x34,0x35,0x36,0x08,0x38,0x39,0x3A,0x3B,0x04,0x14,0x3E,0xE1,
    0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
    0x58,0x59,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x70,0x71,0x72,0x73,0x74,0x75,
    0x76,0x77,0x78,0x80,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x9A,0x9B,0x9C,0x9D,0x9E,
    0x9F,0xA0,0xAA,0xAB,0xAC,0x4A,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,
    0xB8,0xB9,0xBA,0xBB,0xBC,0x6A,0xBE,0xBF,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xDA,0xDB,
    0xDC,0xDD,0xDE,0xDF,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
}};

constexpr void ebcdic_to_ascii_buf(const char* src, char* dst, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i)
        dst[i] = static_cast<char>(mf_ebcdic_to_ascii[static_cast<unsigned char>(src[i])]);
}
constexpr void ascii_to_ebcdic_buf(const char* src, char* dst, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i)
        dst[i] = static_cast<char>(mf_ascii_to_ebcdic[static_cast<unsigned char>(src[i])]);
}

/*════════════════════════════════════════════════════════════════
  INSPECT Operations
════════════════════════════════════════════════════════════════*/

[[nodiscard]] constexpr int inspect_tally_char(const char* field, std::size_t len, char c) noexcept {
    int count = 0;
    for (std::size_t i = 0; i < len; ++i) if (field[i] == c) ++count;
    return count;
}

[[nodiscard]] constexpr int inspect_tally_str(const char* field, std::size_t field_len,
                                              const char* str, std::size_t str_len) noexcept {
    int count = 0;
    for (std::size_t i = 0; i + str_len <= field_len; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < str_len && match; ++j) match = (field[i+j] == str[j]);
        if (match) { ++count; i += str_len - 1; }
    }
    return count;
}

constexpr void inspect_replace_char(char* field, std::size_t len, char old_c, char new_c) noexcept {
    for (std::size_t i = 0; i < len; ++i) if (field[i] == old_c) field[i] = new_c;
}

constexpr void inspect_replace_leading(char* field, std::size_t len, char old_c, char new_c) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        if (field[i] == old_c) field[i] = new_c; else break;
    }
}

constexpr void inspect_convert(char* field, std::size_t len,
                               const char* from, const char* to, std::size_t conv_len) noexcept {
    for (std::size_t i = 0; i < len; ++i)
        for (std::size_t j = 0; j < conv_len; ++j)
            if (field[i] == from[j]) { field[i] = to[j]; break; }
}

/*════════════════════════════════════════════════════════════════
  STRING / UNSTRING Operations
════════════════════════════════════════════════════════════════*/

struct cobol_str_src_t  { const char* data; std::size_t len; };
struct cobol_unstr_dst_t { char* data; std::size_t max_len; std::size_t actual_len; };

constexpr std::size_t string_concat(const cobol_str_src_t* sources, int num_sources,
                                    char* dest, std::size_t dest_len) noexcept {
    std::size_t pos = 0;
    for (int i = 0; i < num_sources && pos < dest_len; ++i) {
        const std::size_t n = std::min(sources[i].len, dest_len - pos);
        std::copy(sources[i].data, sources[i].data + n, dest + pos);
        pos += n;
    }
    return pos;
}

constexpr int unstring_delim(const char* src, std::size_t src_len,
                             char delim,
                             cobol_unstr_dst_t* dests, int max_dests) noexcept {
    std::size_t pos = 0;
    int idx = 0;
    while (pos < src_len && idx < max_dests) {
        const std::size_t start = pos;
        while (pos < src_len && src[pos] != delim) ++pos;
        const std::size_t field_len = std::min(pos - start, dests[idx].max_len);
        std::copy(src + start, src + start + field_len, dests[idx].data);
        std::fill(dests[idx].data + field_len, dests[idx].data + dests[idx].max_len, ' ');
        dests[idx].actual_len = field_len;
        ++idx;
        if (pos < src_len) ++pos; /* skip delimiter */
    }
    return idx;
}

/*════════════════════════════════════════════════════════════════
  Date / Time Helpers
  Pure math functions are constexpr; functions using system time are not.
════════════════════════════════════════════════════════════════*/

/* Convert YYYYMMDD string to proleptic Gregorian Julian day number (constexpr) */
[[nodiscard]] constexpr int date_to_julian(const char* yyyymmdd) noexcept {
    const int y = (yyyymmdd[0]-'0')*1000 + (yyyymmdd[1]-'0')*100
                + (yyyymmdd[2]-'0')*10   + (yyyymmdd[3]-'0');
    const int m = (yyyymmdd[4]-'0')*10   + (yyyymmdd[5]-'0');
    const int d = (yyyymmdd[6]-'0')*10   + (yyyymmdd[7]-'0');
    const int a = (14 - m) / 12;
    const int yy = y + 4800 - a;
    const int mm = m + 12 * a - 3;
    return d + (153*mm+2)/5 + 365*yy + yy/4 - yy/100 + yy/400 - 32045;
}

[[nodiscard]] constexpr int date_diff_days(const char* date1, const char* date2) noexcept {
    return date_to_julian(date2) - date_to_julian(date1);
}

/* Not constexpr — uses system time */
inline void get_date_yyyymmdd(char* dest) noexcept {
    const time_t now = time(nullptr);
    const struct tm* t = localtime(&now);
    const int year = t->tm_year + 1900;
    dest[0] = static_cast<char>('0' + year / 1000);
    dest[1] = static_cast<char>('0' + year / 100 % 10);
    dest[2] = static_cast<char>('0' + year / 10  % 10);
    dest[3] = static_cast<char>('0' + year        % 10);
    dest[4] = static_cast<char>('0' + (t->tm_mon + 1) / 10);
    dest[5] = static_cast<char>('0' + (t->tm_mon + 1) % 10);
    dest[6] = static_cast<char>('0' + t->tm_mday / 10);
    dest[7] = static_cast<char>('0' + t->tm_mday % 10);
}

inline void get_time_hhmmss(char* dest) noexcept {
    const time_t now = time(nullptr);
    const struct tm* t = localtime(&now);
    dest[0] = static_cast<char>('0' + t->tm_hour / 10);
    dest[1] = static_cast<char>('0' + t->tm_hour % 10);
    dest[2] = static_cast<char>('0' + t->tm_min  / 10);
    dest[3] = static_cast<char>('0' + t->tm_min  % 10);
    dest[4] = static_cast<char>('0' + t->tm_sec  / 10);
    dest[5] = static_cast<char>('0' + t->tm_sec  % 10);
}

inline void get_timestamp(char* dest) noexcept {
    get_date_yyyymmdd(dest);
    get_time_hhmmss(dest + 8);
}

/*════════════════════════════════════════════════════════════════
  Mainframe Time and Date Types
  Generic representations used across SMF, VSAM, GTF, and other
  mainframe record formats.
════════════════════════════════════════════════════════════════*/

/* Hundredths-of-seconds time — common encoding across many record types */
struct MfTime {
    int hour;
    int min;
    int sec;
    int hundredths;
};

/* YYYYDDD Julian date — 4-byte COMP-3 packed decimal in many record types */
struct MfDate {
    int year;
    int month;
    int day;
    int yday; /* original Julian day-of-year, 1-based */
};

namespace detail {

constexpr void yday_to_md(int year, int yday, int& month, int& day) noexcept {
    constexpr int dom[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    int d = yday;
    for (int m = 0; m < 12; ++m) {
        const int mdays = (m == 1 && leap) ? 29 : dom[m];
        if (d <= mdays) { month = m + 1; day = d; return; }
        d -= mdays;
    }
    month = 12; day = 31; /* fallback: shouldn't happen with valid input */
}

} // namespace detail

/* Decode a raw uint32 hundredths-of-seconds value */
[[nodiscard]] constexpr MfTime decode_mf_time(uint32_t raw) noexcept {
    MfTime t;
    t.hundredths         = static_cast<int>(raw % 100);
    const uint32_t total = raw / 100;
    t.sec  = static_cast<int>(total % 60);
    t.min  = static_cast<int>((total / 60) % 60);
    t.hour = static_cast<int>(total / 3600);
    return t;
}

/* Flatten MfDate to YYYYMMDD integer — convenient for columnar output */
[[nodiscard]] constexpr int32_t mfdate_to_yyyymmdd(const MfDate& d) noexcept {
    return d.year * 10000 + d.month * 100 + d.day;
}

/* Flatten MfTime to seconds since midnight (hundredths precision) */
[[nodiscard]] constexpr double mftime_to_sec(const MfTime& t) noexcept {
    return t.hour * 3600.0 + t.min * 60.0 + t.sec + t.hundredths * 0.01;
}

/* Decode a raw YYYYDDD integer (as extracted from a 4-byte COMP-3 field) */
[[nodiscard]] constexpr MfDate decode_mf_date(int64_t yyyyddd) noexcept {
    MfDate dt;
    dt.yday  = static_cast<int>(yyyyddd % 1000);
    dt.year  = static_cast<int>(yyyyddd / 1000);
    detail::yday_to_md(dt.year, dt.yday, dt.month, dt.day);
    return dt;
}

/*════════════════════════════════════════════════════════════════
  IBM TOD Clock (Store Clock / STCK)
  64-bit; unit is 2^-12 microseconds.  Epoch: 1900-01-01 00:00 UTC.
  Used across z/OS: SMF, GTF, system trace, VSAM, and RMF records.
════════════════════════════════════════════════════════════════*/

inline constexpr uint64_t TOD_EPOCH_OFFSET_SEC = 2208988800ULL; /* 1900→1970 */

/* Seconds since Unix epoch (1970-01-01).  Returns 0 if before Unix epoch. */
[[nodiscard]] constexpr uint64_t tod_to_unix_sec(uint64_t tod) noexcept {
    const uint64_t sec = (tod >> 12) / 1000000ULL;
    return sec > TOD_EPOCH_OFFSET_SEC ? sec - TOD_EPOCH_OFFSET_SEC : 0ULL;
}

/* Sub-second microseconds (0–999999) */
[[nodiscard]] constexpr uint32_t tod_usec(uint64_t tod) noexcept {
    return static_cast<uint32_t>((tod >> 12) % 1000000ULL);
}

/*════════════════════════════════════════════════════════════════
  String Utilities
════════════════════════════════════════════════════════════════*/

/* Strip trailing ASCII spaces — mainframe fixed-width fields are space-padded */
[[nodiscard]] constexpr std::string_view rtrim(std::string_view s) noexcept {
    const auto last = s.find_last_not_of(' ');
    return (last == std::string_view::npos) ? std::string_view{} : s.substr(0, last + 1);
}

/*════════════════════════════════════════════════════════════════
  Temporal Conversions (Howard Hinnant's algorithms)
  Used to convert mainframe MfDate/MfTime to Unix epoch values.
════════════════════════════════════════════════════════════════*/

/* Days since 1970-01-01 for a proleptic Gregorian date. */
[[nodiscard]] constexpr int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? static_cast<unsigned>(-3) : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

/* Inverse of days_from_civil: epoch days -> (year, month, day). */
constexpr void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365u * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5u * doy + 2) / 153;
    d = doy - (153u * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(yoe) + static_cast<int>(era * 400) + (m <= 2);
}

[[nodiscard]] constexpr bool is_plausible_date(const MfDate& d) noexcept {
    return d.year >= 1970 && d.year <= 9999 &&
           d.month >= 1 && d.month <= 12 &&
           d.day   >= 1 && d.day   <= 31;
}

/* date32 value (epoch days) */
[[nodiscard]] constexpr int32_t mf_date_to_epoch_days(const MfDate& d) noexcept {
    if (!is_plausible_date(d)) return 0;
    return static_cast<int32_t>(days_from_civil(d.year, static_cast<unsigned>(d.month), static_cast<unsigned>(d.day)));
}

/* timestamp[us, UTC] value (epoch microseconds) */
[[nodiscard]] constexpr int64_t mf_datetime_to_unix_us(const MfDate& d, const MfTime& t) noexcept {
    if (!is_plausible_date(d)) return 0;
    const int64_t days = days_from_civil(d.year, static_cast<unsigned>(d.month), static_cast<unsigned>(d.day));
    const int64_t sod_us =
        (static_cast<int64_t>(t.hour) * 3600 + static_cast<int64_t>(t.min) * 60 + t.sec) * 1000000LL +
        static_cast<int64_t>(t.hundredths) * 10000LL;   // 1 hundredth = 10,000 µs
    return days * 86400LL * 1000000LL + sod_us;
}

/*════════════════════════════════════════════════════════════════
  Metadata-Driven Field Access
════════════════════════════════════════════════════════════════*/

enum class FieldType {
    String,
    Int64,
    Double,
    Timestamp,
    Date
};

struct FieldValue {
    FieldType type;
    union {
        const char* s_val; // For String, points to string inside record (must outlive FieldValue)
        int64_t     i_val; // For Int64, Timestamp, Date (epoch values)
        double      d_val; // For Double
    } v;
    size_t s_len{0};

    static FieldValue from_string(const std::string& s) {
        FieldValue f; f.type = FieldType::String; f.v.s_val = s.c_str(); f.s_len = s.length(); return f;
    }
    static FieldValue from_int64(int64_t i) {
        FieldValue f; f.type = FieldType::Int64; f.v.i_val = i; return f;
    }
    static FieldValue from_double(double d) {
        FieldValue f; f.type = FieldType::Double; f.v.d_val = d; return f;
    }
    static FieldValue from_timestamp(int64_t us) {
        FieldValue f; f.type = FieldType::Timestamp; f.v.i_val = us; return f;
    }
    static FieldValue from_date(int32_t days) {
        FieldValue f; f.type = FieldType::Date; f.v.i_val = days; return f;
    }
    static FieldValue null() {
        FieldValue f; f.type = FieldType::String; f.v.s_val = nullptr; f.s_len = 0; return f;
    }
    bool is_null() const { return type == FieldType::String && v.s_val == nullptr; }
};

} // namespace mf
