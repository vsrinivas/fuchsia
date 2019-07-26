// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <utf_conversion/utf_conversion.h>
#include <zircon/assert.h>

namespace {

enum class Endianness {
  HOST,
  INVERT,
};

#if BYTE_ORDER == BIG_ENDIAN
constexpr Endianness kBigEndian = Endianness::HOST;
constexpr Endianness kLittleEndian = Endianness::INVERT;
#else
constexpr Endianness kBigEndian = Endianness::INVERT;
constexpr Endianness kLittleEndian = Endianness::HOST;
#endif

template <Endianness E>
struct CodeUnit;

template <>
struct CodeUnit<Endianness::HOST> {
  static inline uint16_t Read(uint16_t val) { return val; }
};
template <>
struct CodeUnit<Endianness::INVERT> {
  static inline uint16_t Read(uint16_t val) { return __bswap16(val); }
};

static constexpr bool IsHighSurrogate(uint16_t val) { return ((val >= 0xD800) && (val <= 0xDBFF)); }
static constexpr bool IsLowSurrogate(uint16_t val) { return ((val >= 0xDC00) && (val <= 0xDFFF)); }
constexpr uint32_t kMaxUnicodeCodePoint = 0x10FFFF;
constexpr uint32_t kSupplementaryPlaneStart = 0x10000;
constexpr uint32_t kUnicodeReplacementChar = 0xFFFD;

// If there is space to do so, encode the Unicode code point provided as UTF8.
// No matter what, return the number of bytes that the encoded code point would
// take.
//
// If the input is an invalid Unicode codepoint, signal this by returning 0.
inline uint32_t EncodeUtf8CodePoint(uint32_t code_point, uint8_t* tgt, size_t tgt_len,
                                    size_t offset) {
  // If this codepoint is illegal (for whatever reason), replace it with the
  // Unicode replacement character instead.
  if (code_point > kMaxUnicodeCodePoint) {
    code_point = kUnicodeReplacementChar;
  }

  if (code_point < 0x80) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 1)) {
      tgt[offset] = static_cast<uint8_t>(code_point);
    }
    return 1;
  } else if (code_point < 0x800) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 2)) {
      tgt[offset + 0] = static_cast<uint8_t>(0xC0 | (code_point >> 6));
      tgt[offset + 1] = static_cast<uint8_t>(0x80 | (code_point & 0x3F));
    }
    return 2;
  } else if (code_point < 0x10000) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 3)) {
      tgt[offset + 0] = static_cast<uint8_t>(0xE0 | (code_point >> 12));
      tgt[offset + 1] = static_cast<uint8_t>(0x80 | ((code_point >> 6) & 0x3F));
      tgt[offset + 2] = static_cast<uint8_t>(0x80 | (code_point & 0x3F));
    }
    return 3;
  }

  ZX_DEBUG_ASSERT(code_point <= kMaxUnicodeCodePoint);
  if ((tgt_len > offset) && ((tgt_len - offset) >= 4)) {
    tgt[offset + 0] = static_cast<uint8_t>(0xF0 | (code_point >> 18));
    tgt[offset + 1] = static_cast<uint8_t>(0x80 | ((code_point >> 12) & 0x3F));
    tgt[offset + 2] = static_cast<uint8_t>(0x80 | ((code_point >> 6) & 0x3F));
    tgt[offset + 3] = static_cast<uint8_t>(0x80 | (code_point & 0x3F));
  }
  return 4;
}

template <Endianness E>
zx_status_t Utf16ToUtf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len,
                        uint32_t flags) {
  bool preserve_unpaired = (flags & UTF_CONVERT_FLAG_PRESERVE_UNPAIRED_SURROGATES);
  zx_status_t ret = ZX_OK;
  size_t rd = 0;
  size_t wr = 0;

  ZX_DEBUG_ASSERT((src != nullptr) && (dst_len != nullptr));
  ZX_DEBUG_ASSERT((dst != nullptr) || (*dst_len == 0));

  // Process all of our source characters.  Even if we run out of space in our
  // destination, we need to compute the space that we would have needed.
  while (rd < src_len) {
    uint16_t code_unit = CodeUnit<E>::Read(src[rd++]);
    uint32_t code_point;

    // If this is a high surrogate, go looking for its low surrogate pair.
    if (IsHighSurrogate(code_unit)) {
      uint16_t high = code_unit;

      // Fetch the next code unit, if any, and then attempt to pair it up
      // with this high surrogate.
      code_unit = (rd < src_len) ? CodeUnit<E>::Read(src[rd]) : 0;

      // If the next code unit we peeked at is a low surrogate, then
      // combine high and low to form the code point and then encode that.
      // Otherwise, the high surrogate we have encountered is unpaired and
      // should either be replaced or preserved, depending on our flags.
      if (IsLowSurrogate(code_unit)) {
        constexpr uint32_t SHIFT = 10u;
        constexpr uint32_t MASK = (1u << SHIFT) - 1;
        code_point = ((code_unit & MASK) | (static_cast<uint32_t>(high & MASK) << SHIFT)) +
                     kSupplementaryPlaneStart;
        ++rd;
      } else {
        code_point = preserve_unpaired ? high : kUnicodeReplacementChar;
      }
    } else if (IsLowSurrogate(code_unit) && !preserve_unpaired) {
      code_point = kUnicodeReplacementChar;
    } else {
      code_point = code_unit;
    }

    wr += EncodeUtf8CodePoint(code_point, dst, *dst_len, wr);
  }

  *dst_len = wr;
  return ret;
}

}  // namespace

extern "C" {

zx_status_t utf16_to_utf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len,
                          uint32_t flags) {
  // Sanity check our args.
  constexpr uint32_t ENDIAN_FLAGS =
      UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN | UTF_CONVERT_FLAG_FORCE_BIG_ENDIAN;
  constexpr uint32_t ALL_FLAGS =
      UTF_CONVERT_FLAG_DISCARD_BOM | UTF_CONVERT_FLAG_PRESERVE_UNPAIRED_SURROGATES | ENDIAN_FLAGS;
  // dst_len *must* be provided, and all flags need to be understood.
  if ((dst_len == nullptr) || (flags & ~ALL_FLAGS)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // dst may only be null if dst_len is zero (eg; a sizing operation)
  if ((dst == nullptr) && (*dst_len != 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // handle the special case of an empty source string.
  if (!src || !src_len) {
    *dst_len = 0;
    return ZX_OK;
  }

  // Deal with endian detection.
  Endianness detected;

  constexpr uint16_t HOST_BOM = 0xFEFF;
  constexpr uint16_t INVERT_BOM = 0xFFFE;
  const uint16_t bom = *src;
  bool bom_detected = (bom == HOST_BOM) || (bom == INVERT_BOM);

  if ((flags & ENDIAN_FLAGS) == UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN) {
    detected = kLittleEndian;
  } else if ((flags & ENDIAN_FLAGS) == UTF_CONVERT_FLAG_FORCE_BIG_ENDIAN) {
    detected = kBigEndian;
  } else {
    detected = (bom_detected && (bom == INVERT_BOM)) ? Endianness::INVERT : Endianness::HOST;
  }

  if (bom_detected && (flags & UTF_CONVERT_FLAG_DISCARD_BOM)) {
    ZX_DEBUG_ASSERT(src_len > 0);
    ++src;
    --src_len;
  }

  if (detected == Endianness::INVERT) {
    return Utf16ToUtf8<Endianness::INVERT>(src, src_len, dst, dst_len, flags);
  } else {
    return Utf16ToUtf8<Endianness::HOST>(src, src_len, dst, dst_len, flags);
  }
}

}  // extern "C"
