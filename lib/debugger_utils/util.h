// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#ifdef __Fuchsia__
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>
#endif  // __Fuchsia__

#include "lib/fxl/strings/string_view.h"

namespace debugger_utils {

class ByteBlock;

// Decodes the 2 character ASCII string |hex| and returns the result in
// |out_byte|. Returns false if |hex| contains invalid characters.
bool DecodeByteString(const char hex[2], uint8_t* out_byte);

// Encodes |byte| into a 2 character ASCII string and returns the result in
// |out_hex|.
void EncodeByteString(const uint8_t byte, char out_hex[2]);

// Encodes the array of bytes into hexadecimal ASCII digits and returns the
// result in a string.
std::string EncodeByteArrayString(const uint8_t* bytes, size_t num_bytes);

// Encodes the string into hexadecimal ASCII digits and returns the
// result in a string.
std::string EncodeString(const fxl::StringView& string);

// Decodes the given ASCII string describing a series of bytes and returns the
// bytes. |string| must contain and even number of characters, since each byte
// is represented by two ASCII characters.
std::vector<uint8_t> DecodeByteArrayString(const fxl::StringView& string);

// Same as DecodeByteArrayString but return a string.
std::string DecodeString(const fxl::StringView& string);

// Escapes binary non-printable (based on the current locale) characters in a
// printable format to enable pretty-printing of binary data. For example, '0'
// becomes "\x00".
std::string EscapeNonPrintableString(const fxl::StringView& data);

// Return a string representation of errno value |err|.
std::string ErrnoString(int err);

#ifdef __Fuchsia__
// Return a string representation of |status|.
// This includes both the numeric and text values.
std::string ZxErrorString(zx_status_t status);
#endif  // __Fuchsia__

// Joins multiple strings using the given delimiter. This aims to avoid
// repeated dynamic allocations and simply writes the strings into the given
// pre-allocated buffer.
//
// If the resulting string doesn't fit inside the given buffer this will cause
// an assertion failure.
//
// Returns the size of the resulting string.
size_t JoinStrings(const std::deque<std::string>& strings, const char delimiter,
                   char* buffer, size_t buffer_size);

// An argv abstraction, and easier to type.
using Argv = std::vector<std::string>;

Argv BuildArgv(const fxl::StringView& args);

std::string ArgvToString(const Argv& argv);

// Same as strdup but exit if malloc fails.
char* xstrdup(const char* s);

// Same as basename, except will not modify |path|.
// Returns "" if |path| has a trailing /.
const char* basename(const char* path);

void hexdump_ex(FILE* out, const void* ptr, size_t len, uint64_t disp_addr);

#ifdef __Fuchsia__

// Return the name of exception |type| as a C string.
const char* ExceptionName(zx_excp_type_t type);

// Return the string representation of an exception.
std::string ExceptionToString(zx_excp_type_t type,
                              const zx_exception_context_t& context);

bool ReadString(const ByteBlock& m, zx_vaddr_t vaddr, char* ptr, size_t max);

#endif  // __Fuchsia__

}  // namespace debugger_utils
