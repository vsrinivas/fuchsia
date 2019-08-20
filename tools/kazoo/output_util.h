// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_OUTPUT_UTIL_H_
#define TOOLS_KAZOO_OUTPUT_UTIL_H_

#include "src/lib/fxl/strings/ascii.h"
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

#endif  // TOOLS_KAZOO_OUTPUT_UTIL_H_
