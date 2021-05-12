// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>

#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/binding.h"
#include "testing/fidl/async_loop_for_test.h"
#include "testing/fidl/frobinator_impl.h"

namespace fidl {
namespace {

// This test performs a Person In the Middle (PITM) interception of
// channel traffic between the caller and callee.
// The handle is replaced by a replacement function after being
// intercepted. The purpose of this is to test receive-side handle
// error checking for errors that would otherwise be caught on the
// sendiing side. By replacing the handle in the middle of the
// communication, the send-side checks are skipped.
void RunTest(bool is_failure, const std::function<zx_handle_t(zx_handle_t)>& replace_handle) {
  test::AsyncLoopForTest loop;

  zx::event event;
  zx::event::create(0, &event);
  test::frobinator::FrobinatorPtr ptr;
  zx::channel proxy_ch1 = ptr.NewRequest().TakeChannel();
  ptr.set_error_handler([](zx_status_t status) { ZX_PANIC("shouldn't be called"); });
  ptr->SendEventHandle(std::move(event));

  uint8_t buf_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t buf_handles[1];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  ASSERT_OK(proxy_ch1.read(0, buf_bytes, buf_handles, sizeof(buf_bytes), 1, &actual_bytes,
                           &actual_handles));

  ASSERT_EQ(1, actual_handles);
  buf_handles[0] = replace_handle(buf_handles[0]);

  zx::channel proxy_ch2, ch2;
  ASSERT_OK(zx::channel::create(0, &proxy_ch2, &ch2));
  ASSERT_OK(proxy_ch2.write(0, buf_bytes, actual_bytes, buf_handles, 1));

  test::FrobinatorImpl impl;
  Binding<test::frobinator::Frobinator> binding(&impl);
  bool errored = false;
  binding.set_error_handler([&errored](zx_status_t status) {
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
    errored = true;
  });
  ASSERT_OK(binding.Bind(std::move(ch2)));

  loop.RunUntilIdle();
  ASSERT_EQ(is_failure, errored);
}

void RunSuccessTest(const std::function<zx_handle_t(zx_handle_t)>& replace_handle) {
  RunTest(false, replace_handle);
}
void RunFailureTest(const std::function<zx_handle_t(zx_handle_t)>& replace_handle) {
  RunTest(true, replace_handle);
}

TEST(DecodeHandleRightsTest, SuccessCase) {
  RunSuccessTest([](zx_handle_t handle) -> zx_handle_t { return handle; });
}

TEST(DecodeHandleRightsTest, WrongType) {
  RunFailureTest([](zx_handle_t handle) -> zx_handle_t {
    ZX_ASSERT(ZX_OK == zx_handle_close(handle));
    // Replace an event handle with a port handle.
    zx_handle_t port;
    ZX_ASSERT(ZX_OK == zx_port_create(0, &port));
    return port;
  });
}

TEST(DecodeHandleRightsTest, WrongRights) {
  RunFailureTest([](zx_handle_t handle) -> zx_handle_t {
    // Reduce the handle rights (remove ZX_RIGHT_SIGNAL).
    zx_handle_t reduced;
    ZX_ASSERT(ZX_OK == zx_handle_replace(handle, ZX_RIGHTS_BASIC, &reduced));
    return reduced;
  });
}

}  // namespace
}  // namespace fidl
