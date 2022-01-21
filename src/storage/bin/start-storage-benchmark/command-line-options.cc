// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/start-storage-benchmark/command-line-options.h"

#include <lib/cmdline/args_parser.h>
#include <lib/cmdline/status.h>
#include <lib/fitx/result.h>
#include <zircon/assert.h>

#include <istream>

namespace storage_benchmark {
namespace {

constexpr const char kUsageInto[] =
    R"(Tool for launching filesystems and benchmarking them.

Typical usage:
    run fuchsia-pkg://fuchsia.com/start-storage-benchmark#meta/start-storage-benchmark.cmx \
    --filesystem=memfs --mount-path=/benchmark \
    --benchmark-url=fuchsia-pkg://fuchsia.com/odu#meta/odu.cmx \
    -- --target=/benchmark/file

Arguments appearing after `--` will be forwarded to the benchmark.

Options:

)";

constexpr const char kFilesystemHelp[] = R"(  --filesystem=minfs|fxfs|f2fs
      [Required] Filesystem to benchmark against.)";

constexpr const char kPartitionSizeHelp[] = R"(  --partition-size=<bytes>
      Size of the partition in bytes to create for the filesystem in fvm.
      If not set then the filesystem will be given a single fvm slice.
      If the filesystem is fvm-aware then it can allocate more slices from fvm on its own.)";

constexpr const char kZxcryptHelp[] = R"(  --zxcrypt
      Places the filesystem on top of zxcrypt. Not compatible with memfs.)";

constexpr const char kBenchmarkUrlHelp[] = R"(  --benchmark-url=<fuchsia component url>
      [Required] Component url of the benchmark to run.)";

constexpr const char kMountPathHelp[] = R"(  --mount-path=<path>
      [Required] The path to mount the filesystem at in the benchmark's namespace.)";

CommandLineStatus CommandLineError(const cmdline::ArgsParser<CommandLineOptions> &parser,
                                   std::string_view error_msg) {
  std::ostringstream stream;
  stream << error_msg;
  stream << std::endl;
  stream << kUsageInto;
  stream << parser.GetHelp();
  return fitx::error(stream.str());
}

}  // namespace

std::istream &operator>>(std::istream &is, FilesystemOption &filesystem) {
  std::string name;
  if (!(is >> name)) {
    return is;
  }
  if (name == "minfs") {
    filesystem = FilesystemOption::kMinfs;
  } else if (name == "fxfs") {
    filesystem = FilesystemOption::kFxfs;
  } else if (name == "f2fs") {
    filesystem = FilesystemOption::kF2fs;
  } else if (name == "memfs") {
    filesystem = FilesystemOption::kMemfs;
  } else {
    is.setstate(std::ios::failbit);
  }
  return is;
}

CommandLineStatus ParseCommandLine(int argc, const char *const argv[]) {
  cmdline::ArgsParser<CommandLineOptions> parser;
  parser.AddSwitch("filesystem", 0, kFilesystemHelp, &CommandLineOptions::filesystem);
  parser.AddSwitch("partition-size", 0, kPartitionSizeHelp, &CommandLineOptions::partition_size);
  parser.AddSwitch("zxcrypt", 0, kZxcryptHelp, &CommandLineOptions::zxcrypt);
  parser.AddSwitch("benchmark-url", 0, kBenchmarkUrlHelp, &CommandLineOptions::benchmark_url);
  parser.AddSwitch("mount-path", 0, kMountPathHelp, &CommandLineOptions::mount_path);

  CommandLineOptions options;
  std::vector<std::string> benchmark_options;
  if (auto result = parser.Parse(argc, argv, &options, &benchmark_options); result.has_error()) {
    return CommandLineError(parser, result.error_message());
  }
  if (options.filesystem == FilesystemOption::kUnset) {
    return CommandLineError(parser, "--filesystem must be set.");
  }
  if (options.benchmark_url.empty()) {
    return CommandLineError(parser, "--benchmark-url must be set.");
  }
  if (options.mount_path.empty()) {
    return CommandLineError(parser, "--mount-path must be set.");
  }
  if (options.filesystem == FilesystemOption::kMemfs && options.zxcrypt) {
    return CommandLineError(parser, "memfs cannot be started on zxcrypt.");
  }
  options.benchmark_options = benchmark_options;
  return fitx::ok(options);
}

}  // namespace storage_benchmark
