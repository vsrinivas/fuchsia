// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/command-line-options.h"

#include <lib/fit/result.h>
#include <zircon/assert.h>

#include <sstream>

namespace storage_benchmark {
namespace {

constexpr const char kUsageInto[] =
    R"(Tool for launching filesystems and benchmarking them.

Typical usage:
    run-test-suite \
        fuchsia-pkg://fuchsia.com/start-storage-benchmark#meta/start-storage-benchmark.cm \
        -- --filesystem=memfs --mount-path=/benchmark \
        -- --target=/benchmark/file

Arguments appearing after `--` will be forwarded to the benchmark.

Options:
    --filesystem=minfs|fxfs|f2fs|memfs
         [required] Filesystem to benchmark against
    --partition-size=<bytes>
        Size of the partition in bytes to create for the filesystem in fvm.
        If not set then the filesystem will be given a single fvm slice.
        If the filesystem is fvm-aware then it can allocate more slices from fvm on its own.
    --zxcrypt
        Places the filesystem on top of zxcrypt. Not compatible with memfs.
    --mount-path=<path>
        [required] The path to mount the filesystem at in the benchmark's namespace.
)";

CommandLineStatus CommandLineError(std::string_view error_msg) {
  std::ostringstream stream;
  stream << error_msg;
  stream << std::endl;
  stream << kUsageInto;
  return fit::error(stream.str());
}

}  // namespace

CommandLineStatus ParseCommandLine(const fxl::CommandLine& command_line) {
  CommandLineOptions options;
  std::string opt;
  if (command_line.GetOptionValue("filesystem", &opt)) {
    if (opt == "minfs") {
      options.filesystem = FilesystemOption::kMinfs;
    } else if (opt == "fxfs") {
      options.filesystem = FilesystemOption::kFxfs;
    } else if (opt == "f2fs") {
      options.filesystem = FilesystemOption::kF2fs;
    } else if (opt == "memfs") {
      options.filesystem = FilesystemOption::kMemfs;
    }
  }

  if (command_line.GetOptionValue("partition-size", &opt)) {
    options.partition_size = strtoull(opt.c_str(), nullptr, 0);
  }

  if (command_line.HasOption("zxcrypt")) {
    options.zxcrypt = true;
  }

  if (command_line.GetOptionValue("mount-path", &opt)) {
    options.mount_path = opt;
  }

  options.benchmark_options = command_line.positional_args();

  if (options.filesystem == FilesystemOption::kUnset) {
    return CommandLineError("--filesystem must be set.");
  }
  if (options.mount_path.empty()) {
    return CommandLineError("--mount-path must be set.");
  }
  if (options.filesystem == FilesystemOption::kMemfs && options.zxcrypt) {
    return CommandLineError("memfs cannot be started on zxcrypt.");
  }

  return fit::ok(options);
}

}  // namespace storage_benchmark
