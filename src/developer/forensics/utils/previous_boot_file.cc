// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/previous_boot_file.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"

namespace forensics {

PreviousBootFile PreviousBootFile::FromData(const bool is_first_instance, const std::string& file) {
  return PreviousBootFile(is_first_instance, files::JoinPath("/tmp", file),
                          files::JoinPath("/data", file));
}

PreviousBootFile PreviousBootFile::FromCache(const bool is_first_instance,
                                             const std::string& file) {
  return PreviousBootFile(is_first_instance, files::JoinPath("/tmp", file),
                          files::JoinPath("/cache", file));
}

PreviousBootFile::PreviousBootFile(const bool is_first_instance, const std::string& to,
                                   const std::string& from)
    : current_boot_path_(from), previous_boot_path_(to) {
  if (!is_first_instance) {
    return;
  }

  // Bail if the file doesn't exist.
  if (!files::IsFile(current_boot_path_)) {
    return;
  }

  // Bail if the file can't be read.
  std::string content;
  if (!files::ReadFileToString(current_boot_path_, &content)) {
    FX_LOGS(ERROR) << "Failed to read file " << current_boot_path_;
    return;
  }

  // Create the directory in /tmp the file is in.
  auto split_previous_boot_path =
      fxl::SplitStringCopy(previous_boot_path_, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                           fxl::SplitResult::kSplitWantNonEmpty);

  // Drop the file name from the path.
  if (!split_previous_boot_path.empty()) {
    split_previous_boot_path.pop_back();
  }

  // Create the directory.
  if (!split_previous_boot_path.empty()) {
    files::CreateDirectory(fxl::JoinStrings(split_previous_boot_path, "/"));
  }

  // Copy the file content – we cannot move as the two files are under different namespaces.
  if (!files::WriteFile(previous_boot_path_, content.c_str(), content.size())) {
    FX_LOGS(ERROR) << "Failed to write file " << previous_boot_path_;
    return;
  }

  // Delete the original file.
  if (!files::DeletePath(current_boot_path_, /*recursive=*/true)) {
    FX_LOGS(ERROR) << "Failed to delete " << current_boot_path_;
  }
}

}  // namespace forensics
