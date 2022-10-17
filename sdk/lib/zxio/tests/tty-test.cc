// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pty/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/thread_safety.h>
#include <lib/zx/eventpair.h>
#include <lib/zxio/zxio.h>

#include <zxtest/zxtest.h>

class WindowSizeTtyServer : public fidl::testing::WireTestBase<fuchsia_hardware_pty::Device> {
 public:
  void Init() {
    ASSERT_OK(zx::eventpair::create(0, &event0_, &event1_)) << "failed to allocate eventpair";
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE() << "unexpected message received: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseCompleter::Sync& completer) final {
    completer.ReplySuccess();
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }

  void Query(QueryCompleter::Sync& completer) final {
    const std::string_view kProtocol = fuchsia_hardware_pty::wire::kDeviceProtocolName;
    uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
  }

  void Describe2(Describe2Completer::Sync& completer) final {
    zx::eventpair event;
    zx_status_t status = event1_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
    if (status != ZX_OK) {
      ADD_FAILURE() << "failed to duplicate event: " << zx_status_get_string(status);
      completer.Close(status);
      return;
    }
    fidl::Arena alloc;
    completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribe2Response::Builder(alloc)
                        .event(std::move(event))
                        .Build());
  }

  void SetWindowSize(SetWindowSizeRequestView request,
                     SetWindowSizeCompleter::Sync& completer) final {
    std::lock_guard guard(lock_);
    size_ = request->size;
    completer.Reply(ZX_OK);
  }

  void GetWindowSize(GetWindowSizeCompleter::Sync& completer) final {
    std::lock_guard guard(lock_);
    completer.Reply(ZX_OK, size_);
  }

  using Size = fuchsia_hardware_pty::wire::WindowSize;

  void set_size(Size size) {
    std::lock_guard guard(lock_);
    size_ = size;
  }

  Size size() {
    std::lock_guard g(lock_);
    return size_;
  }

 private:
  zx::eventpair event0_, event1_;
  std::mutex lock_;
  Size size_ FIT_GUARDED(lock_);
};

TEST(Tty, Basic) {
  zx::result device_ends = fidl::CreateEndpoints<fuchsia_hardware_pty::Device>();
  ASSERT_OK(device_ends.status_value());
  auto [device_client, device_server] = std::move(device_ends.value());

  async::Loop device_control_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  WindowSizeTtyServer server;
  ASSERT_NO_FATAL_FAILURE(server.Init());
  fidl::BindServer(device_control_loop.dispatcher(), std::move(device_server), &server);
  device_control_loop.StartThread("device_control_thread");

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(device_client.TakeChannel().release(), &storage));
  zxio_t* io = &storage.io;

  bool tty = false;
  EXPECT_OK(zxio_isatty(io, &tty));
  EXPECT_TRUE(tty);

  EXPECT_OK(zxio_set_window_size(io, 42, 57));

  fuchsia_hardware_pty::wire::WindowSize server_size = server.size();
  EXPECT_EQ(42, server_size.width);
  EXPECT_EQ(57, server_size.height);

  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_OK(zxio_get_window_size(io, &width, &height));

  EXPECT_EQ(42, width);
  EXPECT_EQ(57, height);

  ASSERT_OK(zxio_close(io));

  device_control_loop.Shutdown();
}
