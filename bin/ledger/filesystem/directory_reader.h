// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_
#define PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_

#include <functional>

#include <lib/fit/function.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/filesystem/detached_path.h"

namespace ledger {

class DirectoryReader {
 public:
  // Returns the list of directories and files inside the provided directory.
  static bool GetDirectoryEntries(
      const std::string& directory,
      fit::function<bool(fxl::StringView)> callback);

  // Returns the list of directories and files inside the provided directory.
  static bool GetDirectoryEntriesAt(
      const DetachedPath& directory,
      fit::function<bool(fxl::StringView)> callback);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_
