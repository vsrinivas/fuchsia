// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>

#include "argv.h"

namespace debugger_utils {

Argv BuildArgv(const fxl::StringView& args) {
  Argv result;

  // TODO: quoting, escapes, etc.
  // TODO: tweaks for gdb-like command simplification?
  // (e.g., p/x foo -> p /x foo)

  size_t n = args.size();
  for (size_t i = 0; i < n; ++i) {
    while (i < n && isspace(args[i]))
      ++i;
    if (i == n)
      break;
    size_t start = i;
    ++i;
    while (i < n && !isspace(args[i]))
      ++i;
    result.push_back(args.substr(start, i - start).ToString());
  }

  return result;
}

Argv BuildArgv(const char* const argv[], size_t count) {
  Argv result;

  for (size_t i = 0; i < count; ++i) {
    result.push_back(argv[i]);
  }

  return result;
}

std::string ArgvToString(const Argv& argv) {
  if (argv.size() == 0)
    return "";

  std::string result(argv[0]);

  for (auto a = argv.begin() + 1; a != argv.end(); ++a)
    result += " " + *a;

  return result;
}

}  // namespace inferior_control
