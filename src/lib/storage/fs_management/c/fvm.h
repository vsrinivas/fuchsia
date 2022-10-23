// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_C_FVM_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_C_FVM_H_

#include <lib/zx/result.h>

extern "C" {

zx_status_t fvm_init(int fd, size_t slice_size);
}

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_C_FVM_H_
