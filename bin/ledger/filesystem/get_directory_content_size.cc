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

bool GetDirectoryContentSize(fxl::StringView directory, uint64_t* size) {
  *size = 0;
  std::queue<std::string> directories;
  directories.push(directory.ToString());
  while (!directories.empty()) {
    std::string parent = directories.front();
    directories.pop();
    if (!DirectoryReader::GetDirectoryEntries(
            parent, [&parent, size, &directories](fxl::StringView child) {
              std::string full_path =
                  files::AbsolutePath(parent + "/" + child.ToString());
              if (files::IsDirectory(full_path)) {
                directories.push(full_path);
              } else {
                uint64_t file_size = 0;
                if (!files::GetFileSize(full_path, &file_size)) {
                  FXL_LOG(ERROR) << "Couldn't get file size of " << full_path;
                  return false;
                }
                *size += file_size;
              }
              return true;
            })) {
      FXL_LOG(ERROR) << "Couldn't retrieve contents of " << parent;
      return false;
    }
  }
  return true;
}

}  // namespace ledger
