// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_
#define PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_

#include <lib/fit/function.h>

#include "peridot/bin/ledger/filesystem/detached_path.h"

namespace ledger {

// Returns the list of directories and files inside the provided directory. The
// callback will be called once for each directory and files and is guaranteed
// to never be called again once this method returns. This method will returns
// immediately if |callback| returns |false|. This method will returns |false|
// if an error occured while reading the directory and |true| otherwise.
bool GetDirectoryEntries(const DetachedPath& directory,
                         fit::function<bool(fxl::StringView)> callback);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_FILESYSTEM_DIRECTORY_READER_H_
