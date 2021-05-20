// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/ops.h>
#include <string.h>

#include <zxtest/zxtest.h>

TEST(OpsTest, Close) {
  zxio_ops_t ops;
  memset(&ops, 0, sizeof(ops));
  ops.close = [](zxio_t*) { return ZX_OK; };

  zxio_t io = {};
  ASSERT_EQ(nullptr, zxio_get_ops(&io));

  zxio_init(&io, &ops);

  ASSERT_EQ(&ops, zxio_get_ops(&io));
  ASSERT_OK(zxio_close(&io));
}

TEST(OpsTest, CloseWillInvalidateTheObject) {
  zxio_ops_t ops;
  memset(&ops, 0, sizeof(ops));
  ops.close = [](zxio_t*) { return ZX_OK; };

  zxio_t io = {};
  zxio_init(&io, &ops);
  ASSERT_OK(zxio_close(&io));
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, zxio_close(&io));
  ASSERT_EQ(ZX_ERR_BAD_HANDLE, zxio_release(&io, nullptr));
}
