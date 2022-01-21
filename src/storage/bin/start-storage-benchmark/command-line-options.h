// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_
#define SRC_STORAGE_BIN_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_

#include <lib/cmdline/status.h>
#include <lib/fitx/result.h>

#include <istream>
#include <string>
#include <vector>

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

using CommandLineStatus = fitx::result<std::string, CommandLineOptions>;

// Parses the command line options in |CommandLineOptions|.
// Returns an error string if the command line options are invalid.
CommandLineStatus ParseCommandLine(int argc, const char *const argv[]);

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_STORAGE_BENCHMARK_COMMAND_LINE_OPTIONS_
