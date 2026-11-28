#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace seb::inflate {

inline constexpr size_t MAX_INFLATE_SIZE = 128ULL * 1024 * 1024;

struct BitReader {
    const uint8_t* data;
    size_t len;
    size_t byte_pos = 0;
    uint64_t bit_buf = 0;
    uint8_t bit_cnt = 0;

    inline void ensure(uint8_t n) {
        while (bit_cnt < n) {
            if (byte_pos >= len) {
                throw std::runtime_error("unexpected end of deflate stream");
            }
            bit_buf |= static_cast<uint64_t>(data[byte_pos++]) << bit_cnt;
            bit_cnt += 8;
        }
    }

    inline void refill() {
        while (bit_cnt <= 56 && byte_pos < len) {
            bit_buf |= static_cast<uint64_t>(data[byte_pos++]) << bit_cnt;
            bit_cnt += 8;
        }
    }

    inline uint32_t read_bits(uint8_t n) {
        if (n == 0) return 0;
        ensure(n);
        uint32_t val = static_cast<uint32_t>(bit_buf & ((1ULL << n) - 1ULL));
        bit_buf >>= n;
        bit_cnt -= n;
        return val;
    }

    inline void align_byte() {
        uint8_t drop = bit_cnt & 7;
        bit_buf >>= drop;
        bit_cnt -= drop;
    }

    inline uint16_t read_aligned_u16() {
        align_byte();
        uint32_t lo = read_bits(8);
        uint32_t hi = read_bits(8);
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    inline void copy_aligned_bytes(size_t count, std::vector<uint8_t>& out) {
        align_byte();
        size_t buffered = bit_cnt / 8;
        byte_pos -= buffered;
        bit_buf = 0;
        bit_cnt = 0;
        if (count > len - byte_pos) {
            throw std::runtime_error("unexpected EOF in stored block");
        }
        if (count > MAX_INFLATE_SIZE - out.size()) {
            throw std::runtime_error("decompressed size limit exceeded");
        }
        size_t old_sz = out.size();
        out.resize(old_sz + count);
        if (count > 0) std::memcpy(out.data() + old_sz, data + byte_pos, count);
        byte_pos += count;
    }

    [[nodiscard]] inline size_t bytes_consumed() const noexcept {
        return byte_pos - static_cast<size_t>(bit_cnt / 8);
    }
};

struct HuffTable {
    static constexpr uint8_t LUT_BITS = 9;
    static constexpr size_t LUT_SIZE = 1u << LUT_BITS;

    std::array<uint16_t, 16> counts{};
    std::vector<uint16_t> symbols;
    // Packed entry: bits 0..8 = symbol, bits 9..12 = code_len (0 means > LUT_BITS)
    std::array<uint16_t, LUT_SIZE> lut{};

    static HuffTable build(const uint8_t* lengths, size_t num_syms) {
        HuffTable t;
        for (size_t i = 0; i < num_syms; ++i) {
            uint8_t l = lengths[i];
            if (l >= 16) throw std::runtime_error("huffman code length >= 16");
            if (l > 0) t.counts[l]++;
        }
        std::array<uint16_t, 16> offs{};
        int32_t left = 1;
        for (size_t bits = 1; bits < 16; ++bits) {
            left = (left << 1) - static_cast<int32_t>(t.counts[bits]);
            if (left < 0) throw std::runtime_error("oversubscribed huffman tree");
            offs[bits] = static_cast<uint16_t>(offs[bits - 1] + t.counts[bits - 1]);
        }
        size_t total = 0;
        for (uint16_t c : t.counts) total += c;
        t.symbols.resize(total);

        std::array<uint16_t, 16> next_code{};
        uint16_t code = 0;
        for (size_t bits = 1; bits < 16; ++bits) {
            code = static_cast<uint16_t>((code + t.counts[bits - 1]) << 1);
            next_code[bits] = code;
        }

        t.lut.fill(0);
        for (size_t sym = 0; sym < num_syms; ++sym) {
            uint8_t l = lengths[sym];
            if (l == 0) continue;
            t.symbols[offs[l]++] = static_cast<uint16_t>(sym);
            uint16_t c = next_code[l]++;
            if (l <= LUT_BITS) {
                uint16_t rev = 0;
                for (uint8_t b = 0; b < l; ++b) {
                    rev = static_cast<uint16_t>(
                        (static_cast<unsigned>(rev) << 1) | ((static_cast<unsigned>(c) >> b) & 1u));
                }
                uint16_t entry = static_cast<uint16_t>((static_cast<uint16_t>(l) << 9) | static_cast<uint16_t>(sym));
                uint16_t step = static_cast<uint16_t>(1u << l);
                for (size_t idx = rev; idx < LUT_SIZE; idx += step) {
                    t.lut[idx] = entry;
                }
            }
        }
        return t;
    }

    inline uint16_t decode(BitReader& br) const {
        br.refill();
        if (br.bit_cnt >= 1) {
            uint16_t entry = lut[br.bit_buf & (LUT_SIZE - 1)];
            uint8_t l = static_cast<uint8_t>(entry >> 9);
            if (l != 0 && l <= br.bit_cnt) {
                br.bit_buf >>= l;
                br.bit_cnt -= l;
                return entry & 0x1FFu;
            }
        }
        int32_t code = 0;
        int32_t first = 0;
        int32_t index = 0;
        for (size_t len = 1; len < 16; ++len) {
            code |= static_cast<int32_t>(br.read_bits(1));
            int32_t count = static_cast<int32_t>(counts[len]);
            if (code - count < first) {
                size_t sym_idx = static_cast<size_t>(index + (code - first));
                if (sym_idx >= symbols.size()) throw std::runtime_error("invalid huffman symbol index");
                return symbols[sym_idx];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw std::runtime_error("invalid huffman code");
    }
};

namespace detail {
[[nodiscard]] inline std::vector<uint8_t> inflate_raw_impl(
    const uint8_t* data,
    size_t len,
    size_t hint_size,
    size_t* bytes_consumed_out
) {
    static constexpr std::array<uint16_t, 29> LEN_BASE = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
        67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr std::array<uint8_t, 29> LEN_EXTRA = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr std::array<uint16_t, 30> DIST_BASE = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static constexpr std::array<uint8_t, 30> DIST_EXTRA = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static constexpr std::array<size_t, 19> CL_ORDER = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    BitReader br{data, len};
    std::vector<uint8_t> out;
    const size_t max_reserve = (len <= (SIZE_MAX - 4096) / 64) ? (len * 64 + 4096) : (64ULL * 1024 * 1024);
    const size_t fallback_cap = (len <= max_reserve / 3) ? (len * 3) : max_reserve;
    size_t init_cap = hint_size ? (std::min)(hint_size, max_reserve) : fallback_cap;
    if (init_cap > 64ULL * 1024 * 1024) init_cap = 64ULL * 1024 * 1024;
    out.reserve(init_cap);

    while (true) {
        uint32_t bfinal = br.read_bits(1);
        uint32_t btype = br.read_bits(2);
        if (btype == 0) {
            uint16_t blen = br.read_aligned_u16();
            uint16_t nlen = br.read_aligned_u16();
            if (blen != static_cast<uint16_t>(~nlen)) {
                throw std::runtime_error("stored block length mismatch");
            }
            br.copy_aligned_bytes(blen, out);
        } else if (btype == 1 || btype == 2) {
            HuffTable lit_table;
            HuffTable dist_table;
            if (btype == 1) {
                uint8_t lit_lens[288]{};
                for (int i = 0; i <= 143; ++i) lit_lens[i] = 8;
                for (int i = 144; i <= 255; ++i) lit_lens[i] = 9;
                for (int i = 256; i <= 279; ++i) lit_lens[i] = 7;
                for (int i = 280; i <= 287; ++i) lit_lens[i] = 8;
                uint8_t dist_lens[32];
                std::memset(dist_lens, 5, 32);
                lit_table = HuffTable::build(lit_lens, 288);
                dist_table = HuffTable::build(dist_lens, 32);
            } else {
                size_t hlit = br.read_bits(5) + 257;
                size_t hdist = br.read_bits(5) + 1;
                size_t hclen = br.read_bits(4) + 4;
                uint8_t cl_lens[19]{};
                for (size_t i = 0; i < hclen; ++i) {
                    cl_lens[CL_ORDER[i]] = static_cast<uint8_t>(br.read_bits(3));
                }
                HuffTable cl_table = HuffTable::build(cl_lens, 19);
                uint8_t lengths[320]{};
                size_t total_len = hlit + hdist;
                if (total_len > sizeof(lengths)) {
                    throw std::runtime_error("huffman length table too large");
                }
                size_t idx = 0;
                while (idx < total_len) {
                    uint16_t sym = cl_table.decode(br);
                    if (sym <= 15) {
                        lengths[idx++] = static_cast<uint8_t>(sym);
                    } else if (sym == 16) {
                        if (idx == 0) throw std::runtime_error("invalid repeat code 16");
                        uint8_t prev = lengths[idx - 1];
                        size_t rep = br.read_bits(2) + 3;
                        if (idx + rep > total_len) throw std::runtime_error("huffman length table overflow");
                        std::memset(lengths + idx, prev, rep);
                        idx += rep;
                    } else if (sym == 17) {
                        size_t rep = br.read_bits(3) + 3;
                        if (idx + rep > total_len) throw std::runtime_error("huffman length table overflow");
                        std::memset(lengths + idx, 0, rep);
                        idx += rep;
                    } else if (sym == 18) {
                        size_t rep = br.read_bits(7) + 11;
                        if (idx + rep > total_len) throw std::runtime_error("huffman length table overflow");
                        std::memset(lengths + idx, 0, rep);
                        idx += rep;
                    } else {
                        throw std::runtime_error("invalid code length symbol");
                    }
                }
                lit_table = HuffTable::build(lengths, hlit);
                dist_table = HuffTable::build(lengths + hlit, hdist);
            }

            while (true) {
                uint16_t sym = lit_table.decode(br);
                if (sym < 256) {
                    if (out.size() >= MAX_INFLATE_SIZE) throw std::runtime_error("decompressed size limit exceeded");
                    out.push_back(static_cast<uint8_t>(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    size_t idx = sym - 257;
                    if (idx >= LEN_BASE.size()) throw std::runtime_error("invalid length symbol");
                    size_t match_len = LEN_BASE[idx] + br.read_bits(LEN_EXTRA[idx]);
                    size_t d_sym = dist_table.decode(br);
                    if (d_sym >= DIST_BASE.size()) throw std::runtime_error("invalid distance symbol");
                    size_t dist = DIST_BASE[d_sym] + br.read_bits(DIST_EXTRA[d_sym]);
                    if (dist == 0 || dist > out.size()) throw std::runtime_error("invalid back-reference distance");
                    if (match_len > MAX_INFLATE_SIZE - out.size()) throw std::runtime_error("decompressed size limit exceeded");
                    size_t cur = out.size();
                    out.resize(cur + match_len);
                    if (dist >= match_len) {
                        std::memcpy(out.data() + cur, out.data() + cur - dist, match_len);
                    } else {
                        for (size_t k = 0; k < match_len; ++k) {
                            out[cur + k] = out[cur + k - dist];
                        }
                    }
                }
            }
        } else {
            throw std::runtime_error("invalid deflate block type 3");
        }
        if (bfinal != 0) break;
    }

    if (bytes_consumed_out) {
        *bytes_consumed_out = br.bytes_consumed();
    }
    return out;
}
} // namespace detail

inline constexpr std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> tbl{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        tbl[i] = c;
    }
    return tbl;
}

inline constexpr std::array<uint32_t, 256> CRC32_TABLE = make_crc32_table();

[[nodiscard]] inline uint32_t crc32(const uint8_t* data, size_t len) noexcept {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c = CRC32_TABLE[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

[[nodiscard]] inline std::vector<uint8_t> inflate_raw(const uint8_t* data, size_t len, size_t hint_size = 0) {
    return detail::inflate_raw_impl(data, len, hint_size, nullptr);
}

[[nodiscard]] inline std::vector<uint8_t> gzip_decompress(const uint8_t* data, size_t len) {
    if (!data || len < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8) {
        throw std::runtime_error("invalid gzip header");
    }
    uint8_t flg = data[3];
    size_t pos = 10;
    if (flg & 0x04) {
        if (2 > len - pos) throw std::runtime_error("truncated gzip FEXTRA");
        uint16_t xlen = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        if (xlen > len - pos) throw std::runtime_error("truncated gzip FEXTRA payload");
        pos += xlen;
    }
    if (flg & 0x08) {
        while (pos < len && data[pos] != 0) pos++;
        if (pos >= len) throw std::runtime_error("unterminated gzip FNAME");
        pos++;
    }
    if (flg & 0x10) {
        while (pos < len && data[pos] != 0) pos++;
        if (pos >= len) throw std::runtime_error("unterminated gzip FCOMMENT");
        pos++;
    }
    if (flg & 0x02) {
        if (2 > len - pos) throw std::runtime_error("truncated gzip FHCRC");
        uint16_t fhcrc = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
        if (static_cast<uint16_t>(crc32(data, pos) & 0xFFFFu) != fhcrc) {
            throw std::runtime_error("gzip FHCRC mismatch");
        }
        pos += 2;
    }
    if (pos > len - 8) {
        throw std::runtime_error("truncated gzip stream");
    }
    const uint32_t expected_crc =
        static_cast<uint32_t>(data[len - 8]) |
        (static_cast<uint32_t>(data[len - 7]) << 8) |
        (static_cast<uint32_t>(data[len - 6]) << 16) |
        (static_cast<uint32_t>(data[len - 5]) << 24);
    const uint32_t isize =
        static_cast<uint32_t>(data[len - 4]) |
        (static_cast<uint32_t>(data[len - 3]) << 8) |
        (static_cast<uint32_t>(data[len - 2]) << 16) |
        (static_cast<uint32_t>(data[len - 1]) << 24);

    const size_t deflate_len = len - 8 - pos;
    size_t consumed = 0;
    std::vector<uint8_t> out = detail::inflate_raw_impl(data + pos, deflate_len, isize, &consumed);
    if (consumed != deflate_len) {
        throw std::runtime_error("trailing bytes in gzip deflate stream");
    }
    if (static_cast<uint32_t>(out.size() & 0xFFFFFFFFu) != isize) {
        throw std::runtime_error("gzip ISIZE mismatch");
    }
    if (crc32(out.data(), out.size()) != expected_crc) {
        throw std::runtime_error("gzip CRC-32 mismatch");
    }
    return out;
}

[[nodiscard]] inline std::vector<uint8_t> gzip_decompress(const std::vector<uint8_t>& data) {
    return gzip_decompress(data.data(), data.size());
}

} // namespace seb::inflate