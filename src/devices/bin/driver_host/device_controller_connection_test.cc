// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_controller_connection.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/vmo.h>

#include <thread>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "connection_destroyer.h"
#include "driver_host_context.h"
#include "zx_device.h"

namespace {

TEST(DeviceControllerConnectionTestCase, Creation) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel device_local, device_remote;
  ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

  zx::channel device_local2, device_remote2;
  ASSERT_OK(zx::channel::create(0, &device_local2, &device_remote2));

  std::unique_ptr<DeviceControllerConnection> conn;

  fidl::Client<fuchsia_device_manager::Coordinator> client;
  client.Bind(std::move(device_remote2), ctx.loop().dispatcher());
  ASSERT_NULL(dev->conn.load());
  ASSERT_OK(DeviceControllerConnection::Create(&ctx, dev, std::move(device_remote),
                                               std::move(client), &conn));
  ASSERT_NOT_NULL(dev->conn.load());

  ASSERT_OK(DeviceControllerConnection::BeginWait(std::move(conn), ctx.loop().dispatcher()));
  ASSERT_OK(ctx.loop().RunUntilIdle());

  // Clean up memory. Connection destroyer runs asynchronously.
  {
    fbl::AutoLock lock(&ctx.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx.DriverManagerRemove(std::move(dev));
  }
  ASSERT_OK(ctx.loop().RunUntilIdle());
}

TEST(DeviceControllerConnectionTestCase, PeerClosedDuringReply) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel device_local, device_remote;
  ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

  zx::channel device_local2, device_remote2;
  ASSERT_OK(zx::channel::create(0, &device_local2, &device_remote2));

  class DeviceControllerConnectionTest : public DeviceControllerConnection {
   public:
    DeviceControllerConnectionTest(
        DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel rpc,
        fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client,
        fidl::Client<fuchsia_device_manager::DeviceController>& local)
        : DeviceControllerConnection(ctx, std::move(dev), std::move(rpc),
                                     std::move(coordinator_client)),
          local_(local) {}

    void BindDriver(::fidl::StringView driver_path, ::zx::vmo driver,
                    BindDriverCompleter::Sync& completer) override {
      // Pretend that a device closure happened right before we began
      // processing BindDriver.  Close the other half of the channel, so the reply below will fail
      // from ZX_ERR_PEER_CLOSED.
      completer_ = completer.ToAsync();
      local_.Unbind();
    }

    void UnboundDone() {
      completer_->Reply(ZX_OK, zx::channel());

      fbl::AutoLock lock(&driver_host_context_->api_lock());
      this->dev()->removal_cb = [](zx_status_t) {};
      driver_host_context_->DriverManagerRemove(std::move(this->dev_));
    }

   private:
    fidl::Client<fuchsia_device_manager::DeviceController>& local_;
    std::optional<BindDriverCompleter::Async> completer_;
  };

  fidl::Client<fuchsia_device_manager::DeviceController> client;

  fidl::Client<fuchsia_device_manager::Coordinator> coordinator;
  client.Bind(std::move(device_remote2), ctx.loop().dispatcher());
  std::unique_ptr<DeviceControllerConnectionTest> conn;
  conn = std::make_unique<DeviceControllerConnectionTest>(
      &ctx, std::move(dev), std::move(device_remote), std::move(coordinator), client);
  auto* conn_ref = conn.get();

  ASSERT_OK(DeviceControllerConnectionTest::BeginWait(std::move(conn), ctx.loop().dispatcher()));
  ASSERT_OK(ctx.loop().RunUntilIdle());

  class EventHandler : public fidl::WireAsyncEventHandler<fuchsia_device_manager::DeviceController> {
   public:
    explicit EventHandler(DeviceControllerConnectionTest* connection) : connection_(connection) {}

    bool unbound() const { return unbound_; }

    void Unbound(fidl::UnbindInfo info) override {
      unbound_ = true;
      connection_->UnboundDone();
    }

   private:
    DeviceControllerConnectionTest* const connection_;
    bool unbound_ = false;
  };

  auto event_handler = std::make_shared<EventHandler>(conn_ref);
  ASSERT_OK(client.Bind(std::move(device_local), ctx.loop().dispatcher(), event_handler));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  auto result = client->BindDriver(
      ::fidl::StringView("", 1), std::move(vmo),
      [](fuchsia_device_manager::DeviceController::BindDriverResponse* response) {});
  ASSERT_OK(result.status());

  ASSERT_OK(ctx.loop().RunUntilIdle());
  ASSERT_TRUE(event_handler->unbound());
}

// Verify we do not abort when an expected PEER_CLOSED comes in.
TEST(DeviceControllerConnectionTestCase, PeerClosed) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel device_local, device_remote;
  ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

  zx::channel device_local2, device_remote2;
  ASSERT_OK(zx::channel::create(0, &device_local2, &device_remote2));

  fidl::Client<fuchsia_device_manager::Coordinator> client;
  client.Bind(std::move(device_remote2), ctx.loop().dispatcher());
  std::unique_ptr<DeviceControllerConnection> conn;
  ASSERT_OK(DeviceControllerConnection::Create(&ctx, dev, std::move(device_remote),
                                               std::move(client), &conn));

  ASSERT_OK(DeviceControllerConnection::BeginWait(std::move(conn), ctx.loop().dispatcher()));
  ASSERT_OK(ctx.loop().RunUntilIdle());

  // Perform the device shutdown protocol, since otherwise the driver_host code
  // will assert, since it is unable to handle unexpected connection closures.
  {
    fbl::AutoLock lock(&ctx.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx.DriverManagerRemove(std::move(dev));
    device_local.reset();
  }
  ASSERT_OK(ctx.loop().RunUntilIdle());
}

TEST(DeviceControllerConnectionTestCase, UnbindHook) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel device_local, device_remote;
  ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

  zx::channel device_local2, device_remote2;
  ASSERT_OK(zx::channel::create(0, &device_local2, &device_remote2));

  class DeviceControllerConnectionTest : public DeviceControllerConnection {
   public:
    DeviceControllerConnectionTest(
        DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel rpc,
        fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client)
        : DeviceControllerConnection(ctx, std::move(dev), std::move(rpc),
                                     std::move(coordinator_client)) {}

    void Unbind(UnbindCompleter::Sync& completer) {
      fbl::RefPtr<zx_device> dev = this->dev();
      // Set dev->flags() so that we can check that the unbind hook is called in
      // the test.
      dev->set_flag(DEV_FLAG_DEAD);
      completer.ReplySuccess();
    }
  };

  fidl::Client<fuchsia_device_manager::Coordinator> coordinator;
  coordinator.Bind(std::move(device_remote2), ctx.loop().dispatcher());
  std::unique_ptr<DeviceControllerConnectionTest> conn;
  conn = std::make_unique<DeviceControllerConnectionTest>(
      &ctx, std::move(dev), std::move(device_remote), std::move(coordinator));
  fbl::RefPtr<zx_device> my_dev = conn->dev();
  ASSERT_OK(DeviceControllerConnectionTest::BeginWait(std::move(conn), ctx.loop().dispatcher()));
  ASSERT_OK(ctx.loop().RunUntilIdle());

  fidl::Client<fuchsia_device_manager::DeviceController> client;
  ASSERT_OK(client.Bind(std::move(device_local), ctx.loop().dispatcher()));

  bool unbind_successful = false;
  auto result =
      client->Unbind([&](fuchsia_device_manager::DeviceController::UnbindResponse* response) {
        unbind_successful = response->result.is_response();
      });
  ASSERT_OK(result.status());

  ASSERT_OK(ctx.loop().RunUntilIdle());

  ASSERT_EQ(my_dev->flags(), DEV_FLAG_DEAD);
  ASSERT_TRUE(unbind_successful);

  client.Unbind();

  {
    fbl::AutoLock lock(&ctx.api_lock());
    my_dev->removal_cb = [](zx_status_t) {};
    ctx.DriverManagerRemove(std::move(my_dev));
  }
  ASSERT_OK(ctx.loop().RunUntilIdle());
}

}  // namespace
