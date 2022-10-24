// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_KAZOO_OUTPUT_UTIL_H_
#define ZIRCON_TOOLS_KAZOO_OUTPUT_UTIL_H_

#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/writer.h"

// Outputs a copyright header like the one at the top of this file to |writer|.
void CopyrightHeaderWithCppComments(Writer* writer);

// Outputs a copyright header using '#' as the comment marker.
void CopyrightHeaderWithHashComments(Writer* writer);

// Converts |input| to lowercase, assuming it's entirely ASCII.
std::string ToLowerAscii(const std::string& input);

// Converts |input| to uppercase, assuming it's entirely ASCII.
std::string ToUpperAscii(const std::string& input);

// Maps a name from typical FidlCamelStyle to zircon_snake_style.
std::string CamelToSnake(const std::string& camel_fidl);

// Gets a string representing |type| suitable for output to a C file in userspace.
std::string GetCUserModeName(const Type& type);

// Gets a string representing |type| suitable for output to a C file in a kernel header (rather than
// zx_xyz_t*, this will have user_out_ptr<xyz>, etc.)
std::string GetCKernelModeName(const Type& type);

// Gets a string representing |type| suitable for output to a Go file.
std::string GetGoName(const Type& type);

// Gets a size-compatible Go native type.
std::string GetNativeGoName(const Type& type);

// Ensure argument name isn't a Go keyword.
std::string RemapReservedGoName(const std::string& name);

uint32_t DJBHash(const std::string& str);

enum class SignatureNewlineStyle { kAllOneLine, kOnePerLine };

#endif  // ZIRCON_TOOLS_KAZOO_OUTPUT_UTIL_H_
