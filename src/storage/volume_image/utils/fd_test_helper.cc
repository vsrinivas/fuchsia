// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/fd_test_helper.h"

#include <fcntl.h>
#include <lib/fpromise/result.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fbl/unique_fd.h>

namespace storage::volume_image {

fpromise::result<TempFile, std::string> TempFile::Create() {
  std::string base = std::filesystem::temp_directory_path().generic_string() + "/tmp_XXXXXXX";

  fbl::unique_fd created_file(mkstemp(base.data()));
  if (!created_file.is_valid()) {
    return fpromise::error("Failed to create temporary file at " + base +
                           ". More specifically: " + strerror(errno));
  }
  return fpromise::ok(TempFile(base));
}

TempFile::~TempFile() { unlink(path_.c_str()); }

}  // namespace storage::volume_image
