// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/get_directory_content_size.h"

#include <queue>
#include <string>

#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>

#include "peridot/bin/ledger/filesystem/directory_reader.h"

namespace ledger {

bool GetDirectoryContentSize(DetachedPath directory, uint64_t* size) {
  *size = 0;
  std::queue<DetachedPath> directories;
  directories.push(std::move(directory));
  while (!directories.empty()) {
    DetachedPath parent = std::move(directories.front());
    directories.pop();
    if (!GetDirectoryEntries(parent, [&parent, size,
                                      &directories](fxl::StringView child) {
          DetachedPath child_path = parent.SubPath(child);
          if (files::IsDirectoryAt(child_path.root_fd(), child_path.path())) {
            directories.push(child_path);
          } else {
            uint64_t file_size = 0;
            if (!files::GetFileSizeAt(child_path.root_fd(), child_path.path(),
                                      &file_size)) {
              FXL_LOG(ERROR)
                  << "Couldn't get file size of " << child_path.path();
              return false;
            }
            *size += file_size;
          }
          return true;
        })) {
      FXL_LOG(ERROR) << "Couldn't retrieve contents of " << parent.path();
      return false;
    }
  }
  return true;
}

}  // namespace ledger
