// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_controller_connection.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/vmo.h>

#include <thread>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "driver_host_context.h"
#include "zx_device.h"

namespace {

TEST(DeviceControllerConnectionTestCase, Creation) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  fidl::WireSharedClient client(std::move(coordinator_endpoints->client), ctx.loop().dispatcher());
  {
    fbl::AutoLock al(&dev->controller_lock);
    ASSERT_FALSE(dev->controller_binding.has_value());
  }
  auto conn = DeviceControllerConnection::Create(&ctx, dev, std::move(client));

  DeviceControllerConnection::Bind(std::move(conn), std::move(controller_endpoints->server),
                                   ctx.loop().dispatcher());
  {
    fbl::AutoLock al(&dev->controller_lock);
    ASSERT_TRUE(dev->controller_binding.has_value());
  }
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

  zx::status device_ends = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(device_ends.status_value());
  auto [device_local, device_remote] = std::move(*device_ends);

  zx::status device_ends2 = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(device_ends2.status_value());
  auto [device_local2, device_remote2] = std::move(*device_ends2);

  class DeviceControllerConnectionTest : public DeviceControllerConnection {
   public:
    DeviceControllerConnectionTest(
        DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
        fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client,
        fidl::WireSharedClient<fuchsia_device_manager::DeviceController>& local)
        : DeviceControllerConnection(ctx, std::move(dev), std::move(coordinator_client)),
          local_(local) {}

    void BindDriver(BindDriverRequestView request, BindDriverCompleter::Sync& completer) override {
      // Pretend that a device closure happened right before we began
      // processing BindDriver.  Close the other half of the channel, so the reply below will fail
      // from ZX_ERR_PEER_CLOSED.
      completer_ = completer.ToAsync();
      local_.AsyncTeardown();
    }

    void UnboundDone() {
      completer_->Reply(ZX_OK, zx::channel());

      fbl::AutoLock lock(&driver_host_context_->api_lock());
      this->dev()->removal_cb = [](zx_status_t) {};
      driver_host_context_->DriverManagerRemove(this->dev_);
    }

   private:
    fidl::WireSharedClient<fuchsia_device_manager::DeviceController>& local_;
    std::optional<BindDriverCompleter::Async> completer_;
  };

  fidl::WireSharedClient client(std::move(device_local2), ctx.loop().dispatcher());
  fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator;
  auto conn = std::make_unique<DeviceControllerConnectionTest>(&ctx, std::move(dev),
                                                               std::move(coordinator), client);
  auto* conn_ref = conn.get();

  DeviceControllerConnectionTest::Bind(std::move(conn), std::move(device_remote),
                                       ctx.loop().dispatcher());
  ASSERT_OK(ctx.loop().RunUntilIdle());

  bool unbound = false;
  client = fidl::WireSharedClient(std::move(device_local), ctx.loop().dispatcher(),
                                  fidl::ObserveTeardown([&unbound, conn_ref] {
                                    unbound = true;
                                    conn_ref->UnboundDone();
                                  }));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  client->BindDriver(
      ::fidl::StringView(""), std::move(vmo),
      [](fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::BindDriver>&& result) {
        ASSERT_STATUS(ZX_ERR_CANCELED, result.status());
        ASSERT_EQ(fidl::Reason::kUnbind, result.reason());
      });

  ASSERT_OK(ctx.loop().RunUntilIdle());
  ASSERT_TRUE(unbound);
}

// Verify we do not abort when an expected PEER_CLOSED comes in.
TEST(DeviceControllerConnectionTestCase, PeerClosed) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto client =
      fidl::WireSharedClient(std::move(coordinator_endpoints->client), ctx.loop().dispatcher());
  auto conn = DeviceControllerConnection::Create(&ctx, dev, std::move(client));

  DeviceControllerConnection::Bind(std::move(conn), std::move(controller_endpoints->server),
                                   ctx.loop().dispatcher());
  ASSERT_OK(ctx.loop().RunUntilIdle());

  // Perform the device shutdown protocol, since otherwise the driver_host code
  // will assert, since it is unable to handle unexpected connection closures.
  {
    fbl::AutoLock lock(&ctx.api_lock());
    dev->removal_cb = [](zx_status_t) {};
    ctx.DriverManagerRemove(std::move(dev));
    controller_endpoints->client = {};
  }
  ASSERT_OK(ctx.loop().RunUntilIdle());
}

TEST(DeviceControllerConnectionTestCase, UnbindHook) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);

  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::status device_ends = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(device_ends.status_value());

  zx::status coordinator_ends = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_ends.status_value());

  class DeviceControllerConnectionTest : public DeviceControllerConnection {
   public:
    DeviceControllerConnectionTest(
        DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
        fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client)
        : DeviceControllerConnection(ctx, std::move(dev), std::move(coordinator_client)) {}

    void Unbind(UnbindRequestView request, UnbindCompleter::Sync& completer) final {
      fbl::RefPtr<zx_device> dev = this->dev();
      // Set dev->flags() so that we can check that the unbind hook is called in
      // the test.
      dev->set_flag(DEV_FLAG_DEAD);
      completer.ReplySuccess();
    }
  };

  fidl::WireSharedClient coordinator(std::move(coordinator_ends->client), ctx.loop().dispatcher());
  auto conn = std::make_unique<DeviceControllerConnectionTest>(&ctx, std::move(dev),
                                                               std::move(coordinator));
  fbl::RefPtr<zx_device> my_dev = conn->dev();
  DeviceControllerConnectionTest::Bind(std::move(conn), std::move(device_ends->server),
                                       ctx.loop().dispatcher());
  ASSERT_OK(ctx.loop().RunUntilIdle());

  fidl::WireClient client(std::move(device_ends->client), ctx.loop().dispatcher());

  bool unbind_successful = false;
  client->Unbind(
      [&](fidl::WireUnownedResult<fuchsia_device_manager::DeviceController::Unbind>&& result) {
        ASSERT_OK(result.status());
        unbind_successful = result->result.is_response();
      });

  ASSERT_OK(ctx.loop().RunUntilIdle());

  ASSERT_EQ(my_dev->flags(), DEV_FLAG_DEAD);
  ASSERT_TRUE(unbind_successful);

  client = {};

  {
    fbl::AutoLock lock(&ctx.api_lock());
    my_dev->removal_cb = [](zx_status_t) {};
    ctx.DriverManagerRemove(std::move(my_dev));
  }
  ASSERT_OK(ctx.loop().RunUntilIdle());
}

}  // namespace
