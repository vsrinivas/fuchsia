// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_ARGV_H_
#define GARNET_LIB_DEBUGGER_UTILS_ARGV_H_

#include <string>
#include <vector>

#include <lib/fxl/strings/string_view.h>

namespace debugger_utils {

// An argv abstraction, and easier to type.
using Argv = std::vector<std::string>;

Argv BuildArgv(const fxl::StringView& args);

Argv BuildArgv(const char* const argv[], size_t count);

std::string ArgvToString(const Argv& argv);

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_ARGV_H_
