// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_EXTRACTOR_BIN_PARSE_H_
#define SRC_STORAGE_EXTRACTOR_BIN_PARSE_H_

#include <lib/zx/status.h>

#include <optional>
#include <string>

#include <fbl/unique_fd.h>

namespace extractor {

enum class SubCommand {
  // Extract a disk to a file.
  kExtract,

  // Deflate an extracted image.
  kDeflate,
};

enum class DiskType {
  kMinfs,
  kBlobfs,
  kFvm,
};

struct ExtractOptions {
  SubCommand sub_command;

  // Disk/input path from where disk will be extracted.
  std::string input_path;
  fbl::unique_fd input_fd;

  // Image/output path where extracted image will be written.
  std::string output_path;
  fbl::unique_fd output_fd;

  std::optional<DiskType> type = std::nullopt;

  // If true, dumps pii along with metadata.
  bool dump_pii = false;

  bool verbose = false;
};

zx::result<ExtractOptions> ParseCommandLineArguments(int argc, char* const argv[]);

}  // namespace extractor

#endif  // SRC_STORAGE_EXTRACTOR_BIN_PARSE_H_
