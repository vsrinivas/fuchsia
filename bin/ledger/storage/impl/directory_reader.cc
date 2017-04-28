// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/directory_reader.h"

#include <functional>
#include <memory>

#include <dirent.h>

#include "lib/ftl/strings/string_view.h"

namespace storage {
namespace {

void SafeCloseDir(DIR* dir) {
  if (dir)
    closedir(dir);
}

}  // namespace

bool DirectoryReader::GetDirectoryEntries(
    ftl::StringView directory,
    std::function<bool(ftl::StringView)> callback) {
  std::unique_ptr<DIR, decltype(&SafeCloseDir)> dir(opendir(directory.data()),
                                                    SafeCloseDir);
  if (!dir.get())
    return false;
  for (struct dirent* entry = readdir(dir.get()); entry != nullptr;
       entry = readdir(dir.get())) {
    char* name = entry->d_name;
    if (name[0]) {
      if (name[0] == '.') {
        if (!name[1] || (name[1] == '.' && !name[2])) {
          // . or ..
          continue;
        }
      }
      if (!callback(ftl::StringView(name))) {
        break;
      }
    }
  }
  return true;
}

};  // namespace storage
