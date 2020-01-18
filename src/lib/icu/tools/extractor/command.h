// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMAND_H_
#define SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMAND_H_

#include <string>
#include <string_view>

#include "src/lib/fxl/command_line.h"

namespace icu_data_extractor {

// Base class for extractor commands
class Command {
 public:
  virtual ~Command() = default;

  // Provides the name of the command.
  virtual std::string_view Name() const = 0;

  // Executes the command.
  //
  // Parameters:
  //
  //   command_line: The complete icu_data_extractor command line from argc and argv.
  //   sub_command_line: The sub-command, starting with <Name()>.
  virtual int Execute(const fxl::CommandLine& command_line,
                      const fxl::CommandLine& sub_command_line) const = 0;

  // Prints --help documentation for this command.
  virtual void PrintDocs(std::ostream& os) const = 0;
};

}  // namespace icu_data_extractor

#endif  // SRC_LIB_ICU_TOOLS_EXTRACTOR_COMMAND_H_
