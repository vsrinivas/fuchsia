// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/zx/handle.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

TEST(FdioTestCase, DeviceClone) {
  fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

  zx::handle handle;
  ASSERT_OK(fdio_fd_clone(fd.get(), handle.reset_and_get_address()));
  ASSERT_NE(handle.get(), ZX_HANDLE_INVALID);
}

TEST(FdioTestCase, DeviceTransfer) {
  fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

  zx::handle handle;
  ASSERT_OK(fdio_fd_transfer(fd.release(), handle.reset_and_get_address()));
  ASSERT_NE(handle.get(), ZX_HANDLE_INVALID);
}

}  // namespace
