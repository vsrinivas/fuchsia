// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <zircon/syscalls/types.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

TEST(CreateTest, InvalidArgs) {
  ASSERT_EQ(zxio_create(ZX_HANDLE_INVALID, nullptr), ZX_ERR_INVALID_ARGS);

  zxio_storage_t storage;
  ASSERT_EQ(zxio_create(ZX_HANDLE_INVALID, &storage), ZX_ERR_INVALID_ARGS);

  zx::channel channel0, channel1;
  ASSERT_OK(zx::channel::create(0u, &channel0, &channel1));
  ASSERT_EQ(zxio_create(channel0.release(), nullptr), ZX_ERR_INVALID_ARGS);

  // Make sure that the handle is closed.
  zx_signals_t pending = 0;
  ASSERT_EQ(channel1.wait_one(0u, zx::time::infinite_past(), &pending), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
}

TEST(CreateTest, NotSupported) {
  zx::event event;
  ASSERT_OK(zx::event::create(0u, &event));
  zxio_storage_t storage;
  ASSERT_EQ(zxio_create(event.release(), &storage), ZX_ERR_NOT_SUPPORTED);
  zxio_t* io = &storage.io;
  zx::handle handle;
  ASSERT_OK(zxio_release(io, handle.reset_and_get_address()));
  ASSERT_OK(zxio_close(io));

  zx_info_handle_basic_t info = {};
  ASSERT_OK(handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT);
}
