// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/storage/bin/fvm/mtd.h"

zx_status_t CreateFileWrapperFromMtd(const char* path, uint32_t offset, uint32_t max_bad_blocks,
                                     std::unique_ptr<fvm::host::FileWrapper>* wrapper) {
  fprintf(stderr, "Creating FileWrapper from MTD is not supported\n");
  return ZX_ERR_NOT_SUPPORTED;
}
