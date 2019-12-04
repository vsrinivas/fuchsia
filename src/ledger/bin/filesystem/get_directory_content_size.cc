// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/filesystem/get_directory_content_size.h"

#include <queue>
#include <string>

#include "src/ledger/bin/filesystem/directory_reader.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

bool GetDirectoryContentSize(FileSystem* file_system, DetachedPath directory, uint64_t* size) {
  *size = 0;
  std::queue<DetachedPath> directories;
  directories.push(std::move(directory));
  while (!directories.empty()) {
    DetachedPath parent = std::move(directories.front());
    directories.pop();
    if (!GetDirectoryEntries(
            parent, [file_system, &parent, size, &directories](absl::string_view child) {
              DetachedPath child_path = parent.SubPath(child);
              if (file_system->IsDirectory(child_path)) {
                directories.push(child_path);
              } else {
                uint64_t file_size = 0;
                if (!file_system->GetFileSize(child_path, &file_size)) {
                  FXL_LOG(ERROR) << "Couldn't get file size of " << child_path.path();
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
