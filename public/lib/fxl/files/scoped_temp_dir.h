// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_SCOPED_TEMP_DIR_H_
#define LIB_FXL_FILES_SCOPED_TEMP_DIR_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace files {

class FXL_EXPORT ScopedTempDir {
 public:
  ScopedTempDir();
  explicit ScopedTempDir(fxl::StringView parent_path);
  ~ScopedTempDir();

  const std::string& path();

  bool NewTempFile(std::string* output);

 private:
  std::string directory_path_;
};

}  // namespace files

#endif  // LIB_FXL_FILES_SCOPED_TEMP_DIR_H_
