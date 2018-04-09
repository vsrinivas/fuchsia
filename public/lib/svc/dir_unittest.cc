// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/dir.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/zx/channel.h>
#include <fdio/util.h>

#include <thread>

#include "gtest/gtest.h"

namespace svc {
namespace {

static void connect(void* context, const char* service_name, zx_handle_t service_request) {
  async_loop_t* loop = static_cast<async_loop_t*>(context);

  EXPECT_EQ(std::string("foobar"), service_name);
  zx::channel binding(service_request);
  zx_signals_t observed;
  EXPECT_EQ(ZX_OK, binding.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &observed));
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, binding.read(ZX_CHANNEL_READ_MAY_DISCARD, 0, 0, 0, 0, 0, 0));
  EXPECT_EQ(ZX_OK, binding.write(0, "ok", 2, 0, 0));

  async_loop_shutdown(loop);
}

TEST(Service, Control) {
  zx::channel dir, dir_request;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &dir, &dir_request));
  std::thread child([dir_request = std::move(dir_request)]() mutable {

    async::Loop loop(&kAsyncLoopConfigMakeDefault);
    async_t* async = loop.async();

    svc_dir_t* dir = NULL;
    EXPECT_EQ(ZX_OK, svc_dir_create(async, dir_request.release(), &dir));
    EXPECT_EQ(ZX_OK, svc_dir_add_service(dir, "public", "foobar", loop.loop(), connect));
    EXPECT_EQ(ZX_OK, svc_dir_add_service(dir, "public", "baz", NULL, NULL));
    EXPECT_EQ(ZX_ERR_ALREADY_EXISTS, svc_dir_add_service(dir, "public", "baz", NULL, NULL));
    EXPECT_EQ(ZX_OK, svc_dir_add_service(dir, "another", "qux", NULL, NULL));

    loop.Run();

    svc_dir_destroy(dir);
  });

  zx::channel svc, request;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &svc, &request));
  fdio_service_connect_at(dir.get(), "public/foobar", request.release());
  EXPECT_EQ(ZX_OK, svc.write(0, "hello", 5, 0, 0));
  zx_signals_t observed;
  EXPECT_EQ(ZX_OK, svc.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), &observed));
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, svc.read(ZX_CHANNEL_READ_MAY_DISCARD, 0, 0, 0, 0, 0, 0));

  child.join();

  EXPECT_EQ(ZX_OK, zx::channel::create(0, &svc, &request));
  fdio_service_connect_at(dir.get(), "public/foobar", request.release());
  EXPECT_EQ(ZX_OK, svc.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &observed));
}

}  // namespace
}  // namespace svc
