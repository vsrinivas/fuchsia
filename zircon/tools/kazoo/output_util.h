// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_OUTPUT_UTIL_H_
#define TOOLS_KAZOO_OUTPUT_UTIL_H_

#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/writer.h"

// Outputs a copyright header like the one at the top of this file to |writer|.
// true on success, or false with an error logged.
bool CopyrightHeaderWithCppComments(Writer* writer);

// Outputs a copyright header using '#' as the comment marker. Returns true on
// success, or false with an error logged.
bool CopyrightHeaderWithHashComments(Writer* writer);

// Converts |input| to lowercase, assuming it's entirely ASCII.
std::string ToLowerAscii(const std::string& input);

// Maps a name from typical FidlCamelStyle to zircon_snake_style.
std::string CamelToSnake(const std::string& camel_fidl);

// Gets a string representing |type| suitable for output to a C file in userspace.
std::string GetCUserModeName(const Type& type);

// Gets a string representing |type| suitable for output to a C file in a kernel header (rather than
// zx_xyz_t*, this will have user_out_ptr<xyz>, etc.)
std::string GetCKernelModeName(const Type& type);

struct JsonTypeNameData {
  std::string name;
  bool is_pointer{false};
  std::string attribute;
};

// Gets a filled out JsonTypeNameData suitable for output to the definitions.json file. This
// includes a type name similar to the GetCUserName, separate indication of whether the type is a
// pointer, and the data for the argument attributes, if any.
JsonTypeNameData GetJsonName(const Type& type);

// Gets a string representing |type| suitable for output to a Go file.
std::string GetGoName(const Type& type);

// Gets a size-compatible Go native type.
std::string GetNativeGoName(const Type& type);

// Ensure argument name isn't a Go keyword.
std::string RemapReservedGoName(const std::string& name);

uint32_t DJBHash(const std::string& str);

enum class SignatureNewlineStyle { kAllOneLine, kOnePerLine };

// Emits a C syscall signature, up to the closing parenthesis of the argument list (but does not
// include any annotations, nor a trailing semi-colon or opening brace (see CDeclaration() as well).
// |prefix| is a string that goes before the entire declaration.
// |name_prefix| is prepended to the function name.
// |non_nulls| is optional; if it's not null, it will be filled with the indices of the parameters
// which the input specification says must be non-null arguments.
void CSignatureLine(const Syscall& syscall, const char* prefix, const char* name_prefix,
                    Writer* writer, SignatureNewlineStyle newline_style,
                    std::vector<std::string>* non_nulls);

// Emits a C header declaration for a syscall.
// |prefix| is a string that goes before the entire declaration.
// |name_prefix| is prepended to the function name.
void CDeclaration(const Syscall& syscall, const char* prefix, const char* name_prefix,
                  Writer* writer);

// Get the Clang attribute that describes the ownership of the handle.
// Returns empty string for non-handle arguments.
std::string GetHandleOwnershipAttribute(const StructMember &arg);

#endif  // TOOLS_KAZOO_OUTPUT_UTIL_H_
