// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FVM_MTD_H_
#define ZIRCON_TOOLS_FVM_MTD_H_

#include <fbl/unique_ptr.h>
#include <fvm-host/file-wrapper.h>

zx_status_t CreateFileWrapperFromMtd(const char* path, uint32_t offset, uint32_t max_bad_blocks,
                                     fbl::unique_ptr<fvm::host::FileWrapper>* wrapper);

#endif  // ZIRCON_TOOLS_FVM_MTD_H_
