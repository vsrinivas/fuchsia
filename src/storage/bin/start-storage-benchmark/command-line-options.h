// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_START_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_H_
#define SRC_STORAGE_BIN_START_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_H_

#include <lib/fit/result.h>

#include <istream>
#include <string>
#include <vector>

#include "src/lib/fxl/command_line.h"
#include "src/lib/storage/fs_management/cpp/format.h"

namespace storage_benchmark {

enum class FilesystemOption {
  kUnset,
  kMinfs,
  kFxfs,
  kF2fs,
  kMemfs,
};

struct CommandLineOptions {
  FilesystemOption filesystem = FilesystemOption::kUnset;
  uint64_t partition_size = 0;
  bool zxcrypt = false;
  std::string benchmark_url;
  std::string mount_path;
  std::vector<std::string> benchmark_options;
};

using CommandLineStatus = fit::result<std::string, CommandLineOptions>;

// Parses the command line options in |CommandLineOptions|.
// Returns an error string if the command line options are invalid.
CommandLineStatus ParseCommandLine(const fxl::CommandLine& command_line);

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_START_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_H_
