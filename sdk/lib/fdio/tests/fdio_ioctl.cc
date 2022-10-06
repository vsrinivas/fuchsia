// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <sys/ioctl.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

zx_status_t test_ioctl(zxio_t* io, int request, int16_t* out_code, va_list va) {
  *out_code = static_cast<int16_t>(request);
  return ZX_OK;
}

static constexpr zxio_ops_t test_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.ioctl = test_ioctl;
  return ops;
}();

int open_test_fd() {
  zxio_storage_t* storage = nullptr;
  fdio_t* fdio = fdio_zxio_create(&storage);
  zxio_t* zxio = fdio_get_zxio(fdio);
  zxio_init(zxio, &test_ops);
  return fdio_bind_to_fd(fdio, -1, 0);
}

// Test ioctl first use the common ioctl semantic, before using the defined
// ioctl ops.
TEST(IoctlTest, IoctlOpsTest) {
  fbl::unique_fd test_fd(open_test_fd());
  ASSERT_TRUE(test_fd.is_valid());

  // TIOCGWINSZ is handled by fdio directly.
  ASSERT_EQ(ioctl(test_fd.get(), TIOCGWINSZ, nullptr), -1);
  ASSERT_EQ(errno, ENOTTY);

  // Non standard ioctl are delegated to the ioctl op.
  ASSERT_EQ(ioctl(test_fd.get(), 42), -1);
  ASSERT_EQ(errno, 42);
}
}  // namespace
