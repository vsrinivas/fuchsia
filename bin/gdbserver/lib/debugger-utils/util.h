// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include <magenta/syscalls/exception.h>
#include <magenta/types.h>

#include "lib/ftl/strings/string_view.h"

namespace debugserver {
namespace util {

class Memory;

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
std::string EncodeString(const ftl::StringView& string);

// Decodes the given ASCII string describing a series of bytes and returns the
// bytes. |string| must contain and even number of characters, since each byte
// is represented by two ASCII characters.
std::vector<uint8_t> DecodeByteArrayString(const ftl::StringView& string);

// Same as DecodeByteArrayString but return a string.
std::string DecodeString(const ftl::StringView& string);

// Escapes binary non-printable (based on the current locale) characters in a
// printable format to enable pretty-printing of binary data. For example, '0'
// becomes "\x00".
std::string EscapeNonPrintableString(const ftl::StringView& data);

// Logs the given |message|.
void LogError(const std::string& message);

// Logs the given |message| using the global errno variable, including the
// result of strerror in a nicely formatted way.
void LogErrorWithErrno(const std::string& message);

// Logs the given |message| using the string representation of |status| in a
// nicely formatted way.
void LogErrorWithMxStatus(const std::string& message, mx_status_t status);

// Joins multiple strings using the given delimiter. This aims to avoid
// repeated dynamic allocations and simply writes the strings into the given
// pre-allocated buffer.
//
// If the resulting string doesn't fit inside the given buffer this will cause
// an assertion failure.
//
// Returns the size of the resulting string.
size_t JoinStrings(const std::deque<std::string>& strings,
                   const char delimiter,
                   char* buffer,
                   size_t buffer_size);

// Return the name of exception |type| as a C string.
const char* ExceptionName(mx_excp_type_t type);

// Return the string representation of an exception.
std::string ExceptionToString(mx_excp_type_t type,
                              const mx_exception_context_t& context);

bool ReadString(const Memory& m, mx_vaddr_t vaddr, char* ptr, size_t max);

// An argv abstraction, and easier to type.
using Argv = std::vector<std::string>;

Argv BuildArgv(const ftl::StringView& args);

std::string ArgvToString(const Argv& argv);

// Same as strdup but exit if malloc fails.
char* xstrdup(const char* s);

// Same as basename, except will not modify |file|.
// This assumes there are no trailing /s. If there are then |file| is returned
// as is.
const char* basename(const char* s);

void hexdump_ex(FILE* out, const void* ptr, size_t len, uint64_t disp_addr);

}  // namespace util
}  // namespace debugserver
