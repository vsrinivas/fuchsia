// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_FILESYSTEM_GET_DIRECTORY_CONTENT_SIZE_H_
#define SRC_LEDGER_BIN_FILESYSTEM_GET_DIRECTORY_CONTENT_SIZE_H_

#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/platform/platform.h"

namespace ledger {

// Recursively computes the full size of the directory. If a directory (top-level |directory| or any
// of the nested ones) contents can't be retrieved or the size of any of the non-directory entries
// can't be obtained, this will return false and post an error in the log. Otherwise, |size| will
// contain the accumulated size in bytes.
bool GetDirectoryContentSize(FileSystem* file_system, DetachedPath directory, uint64_t* size);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_FILESYSTEM_GET_DIRECTORY_CONTENT_SIZE_H_
