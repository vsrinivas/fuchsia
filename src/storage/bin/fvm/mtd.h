// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_FVM_MTD_H_
#define SRC_STORAGE_BIN_FVM_MTD_H_

#include <memory>

#include "src/storage/fvm/host/file_wrapper.h"

zx_status_t CreateFileWrapperFromMtd(const char* path, uint32_t offset, uint32_t max_bad_blocks,
                                     std::unique_ptr<fvm::host::FileWrapper>* wrapper);

#endif  // SRC_STORAGE_BIN_FVM_MTD_H_
