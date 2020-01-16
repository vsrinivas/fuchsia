// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/pseudo_dir/pseudo_dir_utils.h"

#include "src/lib/fxl/strings/split_string.h"

namespace modular {

std::unique_ptr<vfs::PseudoDir> MakeFilePathWithContents(const std::string& file_path,
                                                         const std::string& file_contents) {
  auto file_path_split =
      fxl::SplitStringCopy(file_path, "/", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  // This is the root-level directory containing |file_path|.
  auto config_dir = std::make_unique<vfs::PseudoDir>();
  auto* last_subdir = config_dir.get();

  // 1. Make each directory in |file_path_split|, except for the last one
  //    which is the file  name.
  for (size_t i = 0; i < file_path_split.size() - 1; i++) {
    auto subdir = std::make_unique<vfs::PseudoDir>();
    auto* subdir_raw = subdir.get();
    last_subdir->AddEntry(file_path_split[i], std::move(subdir));
    last_subdir = subdir_raw;
  }

  // 2. The last component of |file_path_split| is the file -- have it hang off
  // of the last directory.
  last_subdir->AddEntry(file_path_split.back(),
                        std::make_unique<vfs::PseudoFile>(
                            file_contents.size(),
                            [file_contents](std::vector<uint8_t>* out, size_t /*unused*/) {
                              std::copy(file_contents.begin(), file_contents.end(),
                                        std::back_inserter(*out));
                              return ZX_OK;
                            },
                            vfs::PseudoFile::WriteHandler() /* not used */
                            ));

  return config_dir;
}

};  // namespace modular
