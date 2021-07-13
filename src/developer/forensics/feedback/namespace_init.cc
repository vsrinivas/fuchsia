// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/namespace_init.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics::feedback {

bool TestAndSetNotAFdr(const std::string& not_a_fdr_file) {
  if (files::IsFile(not_a_fdr_file)) {
    return true;
  }

  if (!files::WriteFile(not_a_fdr_file, "", 0u)) {
    FX_LOGS(ERROR) << "Failed to create " << not_a_fdr_file;
  }

  return false;
}

void MovePreviousRebootReason(const std::string& from, const std::string& to) {
  // Bail if the file doesn't exist.
  if (!files::IsFile(from)) {
    return;
  }

  // Bail if the file can't be read.
  std::string content;
  if (!files::ReadFileToString(from, &content)) {
    FX_LOGS(ERROR) << "Failed to read file " << from;
    return;
  }

  // Copy the file content – we cannot move as the two files are under different namespaces.
  if (!files::WriteFile(to, content)) {
    FX_LOGS(ERROR) << "Failed to write file " << to;
    return;
  }

  // Delete the original file.
  if (!files::DeletePath(from, /*recursive=*/true)) {
    FX_LOGS(ERROR) << "Failed to delete " << from;
  }
}

}  // namespace forensics::feedback
