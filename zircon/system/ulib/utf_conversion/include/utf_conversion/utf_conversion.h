// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/types.h>

#ifndef UTF_CONVERSION_UTF_CONVERSION_H_
#define UTF_CONVERSION_UTF_CONVERSION_H_

__BEGIN_CDECLS

// Flags which control UTF conversion behavior.
//
// ++ PRESERVE_UNPAIRED_SURROGATES
// By default, when an unpaired surrogates are encountered in a UTF16 stream,
// they will be replaced with the codepoint for the Unicode replacment character
// (U+FFFD).  When the PRESERVE_UNPAIRED_SURROGATE flag is passed, however, the
// value of the unpaired surrogate will be encoded directly as a codepoint.
//
// Note that while the presence of unpaired surrogates are technically a
// violation of the Unicode UTF16 encoding specification, apparently there are
// many UTF16 encoded strings in the world today who have chosen to allow this.
//
// This implementation considers the following to be unpaired surrogates.
// ++ A "high" surrogate [0xD800, 0xDBFF] which is not followed by a "low"
//    surrogate.
// ++ A "low" surrogate [0xDC00, 0xDFFF] which is not preceded by a "high"
//    surrogate.
//
// ++ FORCE_LITTLE_ENDIAN
// ++ FORCE_BIG_ENDIAN
// By default, the conversion process will look for a byte-order-marker (code
// unit 0xFEFF) in order to determine the endianness of the UTF16 source
// string.  If no byte-order-marker is detected, host endianness will be
// assumed.
//
// Users may override this behavior by passing one of the force endian flags.
// The indicated endianness will be assumed, regardless of whether or not a byte
// order marker is found, and anything.  It is illegal to attempt to force both
// big and little endian encoding at the same time.  Attempts to do so will
// result byte-order-marker detection being applied.
//
// ++ DISCARD_BOM
// By default, a byte order marker detected in a UTF16 encoded string will be
// encoded in the UTF8 output.  Users may change this behavior and cause the BOM
// to be discarded instead of encoded by passing the DISCARD_BOM flag.
#define UTF_CONVERT_FLAG_PRESERVE_UNPAIRED_SURROGATES ((uint32_t)0x01)
#define UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN ((uint32_t)0x02)
#define UTF_CONVERT_FLAG_FORCE_BIG_ENDIAN ((uint32_t)0x04)
#define UTF_CONVERT_FLAG_DISCARD_BOM ((uint32_t)0x08)

// Attempt to convert a UTF16 string to UTF8 using either an explicitly
// specified (utf16le_*, utf16be_*) or an unspecified endianness (utf16_*)
//
// src     : a pointer to the source string, encoded using UTF16
// src_len : The number of code units (uint16_t) in the source to process.
// dst     : a pointer to the buffer (not null terminated).
// dst_len : A pointer to the length of of the destination buffer (in bytes).
//           Afterwards, this parameter will be updated to indicate the total
//           number of bytes it would take to hold a representation of the UTF8
//           string (excluding null terminator), even if there was not enough
//           room in the destination buffer to perform a full conversion.  No
//           error is returned if the buffer is not big enough.
// flags   : Flags which control the conversion process.  See above.
//
// Note:  Embedded nulls within the source will be processed and encoded.  *No*
// null termination of the destination buffer will be performed.
#if __cplusplus
zx_status_t utf16_to_utf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len,
                          uint32_t flags = 0);
#else
zx_status_t utf16_to_utf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len,
                          uint32_t flags);
#endif

__END_CDECLS

#endif  // UTF_CONVERSION_UTF_CONVERSION_H_
