// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/scoped_temp_dir.h"

#include "lib/ftl/build_config.h"

// mkdtemp - required include file
#if defined(OS_MACOSX)
#include <unistd.h>
#else
#include <stdlib.h>
#endif

#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace files {

ScopedTempDir::ScopedTempDir() : ScopedTempDir(ftl::StringView()) {}

ScopedTempDir::ScopedTempDir(ftl::StringView parent_path) {
  if (parent_path.empty()) {
    const char* env_var = getenv("TMPDIR");
    parent_path = ftl::StringView(env_var ? env_var : "/tmp");
  }
  // mkdtemp replaces "XXXXXX" so that the resulting directory path is unique.
  directory_path_ = parent_path.ToString() + "/ledger_XXXXXX";
  if (!CreateDirectory(directory_path_) || !mkdtemp(&directory_path_[0])) {
    directory_path_ = "";
  }
}

ScopedTempDir::~ScopedTempDir() {
  if (directory_path_.size()) {
    if (!DeletePath(directory_path_, true)) {
      FTL_LOG(WARNING) << "Unable to delete: " << directory_path_;
    }
  }
}

const std::string& ScopedTempDir::path() {
  return directory_path_;
}

bool ScopedTempDir::NewTempFile(std::string* output) {
  // mkstemp replaces "XXXXXX" so that the resulting file path is unique.
  std::string file_path = directory_path_ + "/XXXXXX";
  ftl::UniqueFD fd(mkstemp(&file_path[0]));
  if (!fd.is_valid()) {
    return false;
  }
  output->swap(file_path);
  return true;
}

}  // namespace files
