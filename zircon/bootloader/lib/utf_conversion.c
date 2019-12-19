// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utf_conversion.h"

static bool IsHighSurrogate(uint16_t val) { return ((val >= 0xD800) && (val <= 0xDBFF)); }
static bool IsLowSurrogate(uint16_t val) { return ((val >= 0xDC00) && (val <= 0xDFFF)); }
const uint32_t kMaxUnicodeCodePoint = 0x10FFFF;
const uint32_t kSupplementaryPlaneStart = 0x10000;
const uint32_t kUnicodeReplacementChar = 0xFFFD;

// If there is space to do so, encode the Unicode code point provided as UTF8.
// No matter what, return the number of bytes that the encoded code point would
// take.
//
// If the input is an invalid Unicode codepoint, signal this by returning 0.
uint32_t EncodeUtf8CodePoint(uint32_t code_point, uint8_t* tgt, size_t tgt_len,
                                    size_t offset) {
  // If this codepoint is illegal (for whatever reason), replace it with the
  // Unicode replacement character instead.
  if (code_point > kMaxUnicodeCodePoint) {
    code_point = kUnicodeReplacementChar;
  }

  if (code_point < 0x80) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 1)) {
      tgt[offset] = (uint8_t)(code_point);
    }
    return 1;
  } else if (code_point < 0x800) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 2)) {
      tgt[offset + 0] = (uint8_t)(0xC0 | (code_point >> 6));
      tgt[offset + 1] = (uint8_t)(0x80 | (code_point & 0x3F));
    }
    return 2;
  } else if (code_point < 0x10000) {
    if ((tgt_len > offset) && ((tgt_len - offset) >= 3)) {
      tgt[offset + 0] = (uint8_t)(0xE0 | (code_point >> 12));
      tgt[offset + 1] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
      tgt[offset + 2] = (uint8_t)(0x80 | (code_point & 0x3F));
    }
    return 3;
  }

  if ((tgt_len > offset) && ((tgt_len - offset) >= 4)) {
    tgt[offset + 0] = (uint8_t)(0xF0 | (code_point >> 18));
    tgt[offset + 1] = (uint8_t)(0x80 | ((code_point >> 12) & 0x3F));
    tgt[offset + 2] = (uint8_t)(0x80 | ((code_point >> 6) & 0x3F));
    tgt[offset + 3] = (uint8_t)(0x80 | (code_point & 0x3F));
  }
  return 4;
}

// If there is space to do so, encode the Unicode code point provided as UTF16.
// No matter what, return the number of 16-bit characters that the encoded code point would
// take.
//
// If the input is an invalid Unicode codepoint, signal this by returning 0.
// Input length is in bytes.
uint32_t EncodeUtf16CodePoint(uint32_t code_point, uint16_t* tgt, size_t tgt_len,
                                    size_t offset) {
  // If this codepoint is illegal (for whatever reason), replace it with the
  // Unicode replacement character instead.
  if (code_point > kMaxUnicodeCodePoint) {
    code_point = kUnicodeReplacementChar;
  }

  // Convert bytes to characters.
  tgt_len /= sizeof(uint16_t);

  // TODO: this only works with single-byte UTF8 characters.
  if ((tgt_len > offset) && ((tgt_len - offset) >= 2)) {
    tgt[offset] = (uint16_t)code_point;
  }
  return 1;
}


zx_status_t Utf16ToUtf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len) {
  zx_status_t ret = ZX_OK;
  size_t rd = 0;
  size_t wr = 0;

  // Process all of our source characters.  Even if we run out of space in our
  // destination, we need to compute the space that we would have needed.
  while (rd < src_len) {
    uint16_t code_unit = src[rd++];
    uint32_t code_point;

    // If this is a high surrogate, go looking for its low surrogate pair.
    if (IsHighSurrogate(code_unit)) {
      uint16_t high = code_unit;

      // Fetch the next code unit, if any, and then attempt to pair it up
      // with this high surrogate.
      code_unit = (rd < src_len) ? src[rd] : 0;

      // If the next code unit we peeked at is a low surrogate, then
      // combine high and low to form the code point and then encode that.
      // Otherwise, the high surrogate we have encountered is unpaired and
      // should either be replaced or preserved, depending on our flags.
      if (IsLowSurrogate(code_unit)) {
        const uint32_t SHIFT = 10u;
        const uint32_t MASK = (1u << SHIFT) - 1;
        code_point = ((code_unit & MASK) | ((uint32_t)(high & MASK) << SHIFT)) +
                     kSupplementaryPlaneStart;
        ++rd;
      } else {
        code_point = kUnicodeReplacementChar;
      }
    } else if (IsLowSurrogate(code_unit)) {
      code_point = kUnicodeReplacementChar;
    } else {
      code_point = code_unit;
    }

    wr += EncodeUtf8CodePoint(code_point, dst, *dst_len, wr);
  }

  *dst_len = wr;
  return ret;
}

zx_status_t Utf8ToUtf16(const uint8_t* src, size_t src_len, uint16_t* dst, size_t* dst_len) {
  zx_status_t ret = ZX_OK;
  size_t rd = 0;
  size_t wr = 0;

  // Process all of our source characters.  Even if we run out of space in our
  // destination, we need to compute the space that we would have needed.
  while (rd < src_len) {
    uint32_t code_point = src[rd++];

    // TODO: should correctly process multibyte characters here.
    wr += EncodeUtf16CodePoint(code_point, dst, *dst_len, wr);
  }

  *dst_len = wr * sizeof(uint16_t);
  return ret;
}

zx_status_t utf16_to_utf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len) {
  // dst_len *must* be provided.
  if (dst_len == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  // dst may only be null if dst_len is zero (eg; a sizing operation)
  if ((dst == NULL) && (*dst_len != 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // handle the special case of an empty source string.
  if (!src || !src_len) {
    *dst_len = 0;
    return ZX_OK;
  }

  const uint16_t HOST_BOM = 0xFEFF;
  const uint16_t INVERT_BOM = 0xFFFE;
  const uint16_t bom = *src;
  bool bom_detected = (bom == HOST_BOM) || (bom == INVERT_BOM);

  if (bom_detected) {
    ++src;
    --src_len;
  }

  return Utf16ToUtf8(src, src_len, dst, dst_len);
}

zx_status_t utf8_to_utf16(const uint8_t* src, size_t src_len, uint16_t* dst, size_t* dst_len) {
  // dst_len *must* be provided.
  if (dst_len == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  // dst may only be null if dst_len is zero (eg; a sizing operation)
  if ((dst == NULL) && (*dst_len != 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // handle the special case of an empty source string.
  if (!src || !src_len) {
    *dst_len = 0;
    return ZX_OK;
  }

  return Utf8ToUtf16(src, src_len, dst, dst_len);
}
