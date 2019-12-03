// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_VMO_FILE_H_
#define SRC_LEDGER_LIB_VMO_FILE_H_

#include <lib/zx/vmo.h>

#include <string>

#include "src/ledger/lib/vmo/sized_vmo.h"
#include "src/lib/files/unique_fd.h"

namespace ledger {

// Make a new shared buffer with the contents of a file.
bool VmoFromFd(fbl::unique_fd fd, SizedVmo* handle_ptr);

// Make a new shared buffer with the contents of a file.
bool VmoFromFilename(const std::string& filename, SizedVmo* handle_ptr);

// Make a new shared buffer with the contents of a file.
bool VmoFromFilenameAt(int dirfd, const std::string& filename, SizedVmo* handle_ptr);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_VMO_FILE_H_
