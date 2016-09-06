// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/scoped_temp_dir.h"

#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"

namespace files {

ScopedTempDir::ScopedTempDir() {
  const char* tmp_folder = getenv("TMPDIR");
  if (!tmp_folder) tmp_folder = "/tmp";
  // mkdtemp replaces "XXXXXX" so that the resulting directory path is unique.
  directory_path_ = std::string(tmp_folder) + "/ledger_XXXXXX";
  if (!mkdtemp(&directory_path_[0])) directory_path_ = "";
}

ScopedTempDir::~ScopedTempDir() {
  if (directory_path_.size()) {
    if (!DeletePath(directory_path_, true))
      FTL_LOG(WARNING) << "Unable to delete: " << directory_path_;
  }
}

const std::string& ScopedTempDir::path() { return directory_path_; }

bool ScopedTempDir::NewTempFile(std::string* output) {
  // mkstemp replaces "XXXXXX" so that the resulting file path is unique.
  std::string file_path = directory_path_ + "/XXXXXX";
  ftl::UniqueFD fd(mkstemp(&file_path[0]));
  if (!fd.is_valid()) return false;
  output->swap(file_path);
  return true;
}

}  // namespace files
