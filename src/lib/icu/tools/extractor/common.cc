// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common.h"

#include <iostream>

#include "src/lib/files/file.h"
#include "src/lib/fxl/command_line.h"

namespace icu_data_extractor {

std::optional<std::string> GetOutputPath(const fxl::CommandLine& command_line) {
  if (command_line.HasOption(kArgOutputPath)) {
    std::string output_path_str;
    command_line.GetOptionValue(kArgOutputPath, &output_path_str);
    return output_path_str;
  }
  return std::nullopt;
}

int WriteToOutputFileOrStdOut(const fxl::CommandLine& sub_command_line,
                              const std::string& contents) {
  const auto output_path = GetOutputPath(sub_command_line);
  if (output_path.has_value()) {
    if (!files::WriteFile(output_path.value(), contents.c_str(), contents.length())) {
      std::cerr << "Couldn't write to " << output_path.value() << std::endl;
      return -1;
    }
  } else {
    std::cout << contents;
  }

  return 0;
}

}  // namespace icu_data_extractor
