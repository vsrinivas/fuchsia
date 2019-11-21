// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FILESYSTEM_DIRECTORY_READER_H_
#define SRC_LEDGER_BIN_FILESYSTEM_DIRECTORY_READER_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/filesystem/detached_path.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// Returns the list of directories and files inside the provided directory. The
// callback will be called once for each directory and files and is guaranteed
// to never be called again once this method returns. This method will returns
// immediately if |callback| returns |false|. This method will returns |false|
// if an error occured while reading the directory and |true| otherwise.
bool GetDirectoryEntries(const DetachedPath& directory,
                         fit::function<bool(absl::string_view)> callback);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_FILESYSTEM_DIRECTORY_READER_H_
