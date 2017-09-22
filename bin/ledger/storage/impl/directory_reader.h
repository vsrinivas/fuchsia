// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_DIRECTORY_READER_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_DIRECTORY_READER_H_

#include <functional>

#include "lib/fxl/strings/string_view.h"

namespace storage {

class DirectoryReader {
 public:
  // Returns the list of directories inside the provided directory.
  static bool GetDirectoryEntries(
      fxl::StringView directory,
      std::function<bool(fxl::StringView)> callback);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_DIRECTORY_READER_H_
