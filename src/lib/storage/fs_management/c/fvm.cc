// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/c/fvm.h"

#include <lib/fdio/cpp/caller.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"

zx_status_t fvm_init(int fd, size_t slice_size) {
  fdio_cpp::UnownedFdioCaller caller(fd);
  return fs_management::FvmInit(caller.borrow_as<fuchsia_hardware_block::Block>(), slice_size);
}
