// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/types.h>

#pragma once

// Attempt to convert a UTF16 string to UTF8 using an unspecified endianness (utf16_*).
//
// src     : a pointer to the source string, encoded using UTF16
// src_len : The number of code units (uint16_t) in the source to process.
// dst     : a pointer to the buffer which will hold the null terminated result
//           of the conversion.
// dst_len : A pointer to the length of of the destination buffer (in bytes).
//           Afterwards, this parameter will be updated to indicate the total
//           number of bytes it would take to hold a null terminated
//           representation of the UTF8 string, even if there was not enough
//           room in the destination buffer to perform a full conversion.
//
// Note:  Embedded nulls within the source will be processed and encoded.  *No*
// null termination of the destination buffer will be performed by default.
zx_status_t utf16_to_utf8(const uint16_t* src, size_t src_len, uint8_t* dst, size_t* dst_len);

// Attempt to convert a UTF8 string to UTF16 using an unspecified endianness (utf16_*).
//
// src     : a pointer to the source string, encoded using UTF8
// src_len : The number of bytes in the source to process.
// dst     : a pointer to the buffer which will hold the null terminated result
//           of the conversion.
// dst_len : A pointer to the length of of the destination buffer (in bytes).
//           Afterwards, this parameter will be updated to indicate the total
//           number of bytes it would take to hold a null terminated
//           representation of the UTF8 string, even if there was not enough
//           room in the destination buffer to perform a full conversion.
//
// Note:  Embedded nulls within the source will be processed and encoded.  *No*
// null termination of the destination buffer will be performed by default.

zx_status_t utf8_to_utf16(const uint8_t* src, size_t src_len, uint16_t* dst, size_t* dst_len);
