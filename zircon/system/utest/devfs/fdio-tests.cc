// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

namespace {

TEST(FdioTestCase, DeviceClone) {
  fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_fd_clone(fd.get(), &handle);
  ASSERT_OK(status);
  ASSERT_NE(handle, ZX_HANDLE_INVALID);
  zx_handle_close(handle);
}

TEST(FdioTestCase, DeviceTransfer) {
  fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = fdio_fd_transfer(fd.release(), &handle);
  ASSERT_OK(status);
  ASSERT_NE(handle, ZX_HANDLE_INVALID);
  zx_handle_close(handle);
}

}  // namespace
