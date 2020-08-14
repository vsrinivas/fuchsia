// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOL_INDEX_COMMAND_LINE_OPTIONS_H_
#define TOOLS_SYMBOL_INDEX_COMMAND_LINE_OPTIONS_H_

#include <string>
#include <vector>

#include "tools/symbol-index/error.h"

namespace symbol_index {

struct CommandLineOptions {
  enum class Verb {
    kList,
    kAdd,
    kAddAll,
    kRemove,
    kPurge,
  };
  std::string symbol_index_file;
  Verb verb;
  std::vector<std::string> params;

  // Sets verb from a string. Returns an error message if the string is invalid.
  Error SetVerb(const std::string& str);

  // Validates the length of params with the action.
  Error Validate();
};

// Parses the command line into options.
Error ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options);

}  // namespace symbol_index

#endif  // TOOLS_SYMBOL_INDEX_COMMAND_LINE_OPTIONS_H_
