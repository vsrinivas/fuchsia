// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/device.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fidl/llcpp/connect_service.h>

#include <zxtest/zxtest.h>

#include "src/devices/board/drivers/x86/acpi/manager.h"
#include "src/devices/board/drivers/x86/acpi/test/device.h"
#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

class NotifyHandlerServer : public fidl::WireServer<fuchsia_hardware_acpi::NotifyHandler> {
 public:
  using Callback = std::function<void(uint32_t, HandleCompleter::Sync& completer)>;
  explicit NotifyHandlerServer(Callback cb) : callback_(std::move(cb)) {}
  ~NotifyHandlerServer() override {
    if (ref_ != std::nullopt) {
      Close();
    }
  }

  static fidl::ClientEnd<fuchsia_hardware_acpi::NotifyHandler> CreateAndServe(
      Callback cb, async_dispatcher_t* dispatcher, std::unique_ptr<NotifyHandlerServer>* out) {
    *out = std::make_unique<NotifyHandlerServer>(std::move(cb));
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::NotifyHandler>();
    out->get()->ref_ = fidl::BindServer(dispatcher, std::move(endpoints->server), out->get());
    return std::move(endpoints->client);
  }

  void Handle(HandleRequestView rv, HandleCompleter::Sync& completer) override {
    callback_(rv->value, completer);
  }

  void Close() {
    ref_->Close(ZX_ERR_PEER_CLOSED);
    ref_ = std::nullopt;
  }

 private:
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_acpi::NotifyHandler>> ref_;
  Callback callback_;
};

class AcpiDeviceTest : public zxtest::Test {
 public:
  AcpiDeviceTest() : mock_root_(MockDevice::FakeRootParent()), manager_(&acpi_, mock_root_.get()) {}

  void SetUp() override {
    acpi_.SetDeviceRoot(std::make_unique<acpi::test::Device>("\\"));
    ASSERT_OK(manager_.StartFidlLoop());
  }

  void TearDown() override {
    for (auto& child : mock_root_->children()) {
      device_async_remove(child.get());
    }
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(mock_root_.get()));
  }

  void SetUpFidlServer(std::unique_ptr<acpi::Device> device) {
    ASSERT_OK(device->DdkAdd("test-acpi-device"));

    // Give mock_ddk ownership of the device.
    zx_device_t* dev = device.release()->zxdev();
    dev->InitOp();
    dev->WaitUntilInitReplyCalled(zx::time::infinite());

    ddk::AcpiProtocolClient acpi(dev);
    ASSERT_TRUE(acpi.is_valid());

    fidl::ClientEnd<fuchsia_hardware_acpi::Device> client_end;
    auto server_end = fidl::CreateEndpoints(&client_end);
    ASSERT_OK(server_end.status_value());

    acpi.ConnectServer(server_end->TakeChannel());
    fidl_client_ = fidl::WireSyncClient<fuchsia_hardware_acpi::Device>(std::move(client_end));
  }

 protected:
  std::shared_ptr<MockDevice> mock_root_;
  acpi::Manager manager_;
  acpi::test::MockAcpi acpi_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> fidl_client_;
};

TEST_F(AcpiDeviceTest, TestBanjoConnectServer) {
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), ACPI_ROOT_OBJECT,
                                               mock_root_.get());
  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->GetBusId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_err());
  ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
}

TEST_F(AcpiDeviceTest, TestBanjoConnectServerTwice) {
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), ACPI_ROOT_OBJECT,
                                               mock_root_.get());
  SetUpFidlServer(std::move(device));
  {
    auto result = fidl_client_->GetBusId();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
  }

  // Connect again and make sure it still works.
  ddk::AcpiProtocolClient acpi(mock_root_->GetLatestChild());
  ASSERT_TRUE(acpi.is_valid());
  fidl::ClientEnd<fuchsia_hardware_acpi::Device> client_end2;
  auto server_end = fidl::CreateEndpoints(&client_end2);
  ASSERT_OK(server_end.status_value());

  acpi.ConnectServer(server_end->TakeChannel());

  auto fidl_client2 = fidl::WireSyncClient<fuchsia_hardware_acpi::Device>(std::move(client_end2));
  {
    auto result = fidl_client2->GetBusId();
    ASSERT_OK(result.status());
    ASSERT_TRUE(result.value().result.is_err());
    ASSERT_EQ(result.value().result.err(), ZX_ERR_BAD_STATE);
  }
}

TEST_F(AcpiDeviceTest, TestGetBusId) {
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), ACPI_ROOT_OBJECT,
                                               mock_root_.get(), std::vector<uint8_t>(),
                                               acpi::BusType::kI2c, 37);
  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->GetBusId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result.value().result.is_response());
  ASSERT_EQ(result.value().result.response().bus_id, 37);
}

TEST_F(AcpiDeviceTest, TestAcquireGlobalLockAccessDenied) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());

  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->AcquireGlobalLock();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->result.is_err());
  ASSERT_EQ(result->result.err(), fuchsia_hardware_acpi::wire::Status::kAccess);
}

// _GLK method exists, but returns zero.
TEST_F(AcpiDeviceTest, TestAcquireGlobalLockAccessDeniedButMethodExists) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  test_dev->SetGlk(false);
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());

  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->AcquireGlobalLock();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->result.is_err());
  ASSERT_EQ(result->result.err(), fuchsia_hardware_acpi::wire::Status::kAccess);
}

TEST_F(AcpiDeviceTest, TestAcquireGlobalLockImplicitRelease) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  test_dev->SetGlk(true);
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());

  SetUpFidlServer(std::move(device));

  sync_completion_t acquired;
  sync_completion_t running;
  {
    auto result = fidl_client_->AcquireGlobalLock();
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->result.is_response(), "ACPI error %d", int(result->result.err()));

    std::thread thread([&acquired, &running, this]() {
      sync_completion_signal(&running);
      ASSERT_TRUE(fidl_client_->AcquireGlobalLock().ok());
      sync_completion_signal(&acquired);
    });
    // Make sure thread keeps running when it goes out of scope.
    thread.detach();

    ASSERT_OK(sync_completion_wait(&running, ZX_TIME_INFINITE));
    ASSERT_STATUS(sync_completion_wait(&acquired, ZX_MSEC(50)), ZX_ERR_TIMED_OUT);

    // result, which holds the GlobalLock ClientEnd, will go out of scope here
    // and close the channel, which should release the global lock.
  }

  ASSERT_OK(sync_completion_wait(&acquired, ZX_TIME_INFINITE));
}

TEST_F(AcpiDeviceTest, TestInstallNotifyHandler) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());
  async::Loop server_loop(&kAsyncLoopConfigNeverAttachToThread);

  SetUpFidlServer(std::move(device));
  sync_completion_t done;
  std::unique_ptr<NotifyHandlerServer> server;
  auto client = NotifyHandlerServer::CreateAndServe(
      [&](uint32_t type, NotifyHandlerServer::HandleCompleter::Sync& completer) {
        ASSERT_EQ(type, 32);
        completer.Reply();
        sync_completion_signal(&done);
      },
      manager_.fidl_dispatcher(), &server);

  auto result = fidl_client_->InstallNotifyHandler(
      fuchsia_hardware_acpi::wire::NotificationMode::kSystem, std::move(client));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  hnd->Notify(32);
  sync_completion_wait(&done, ZX_TIME_INFINITE);
}

TEST_F(AcpiDeviceTest, TestNotifyHandlerDropsEvents) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());
  async::Loop server_loop(&kAsyncLoopConfigNeverAttachToThread);

  SetUpFidlServer(std::move(device));
  size_t received_events = 0;
  std::vector<NotifyHandlerServer::HandleCompleter::Async> completers;
  std::unique_ptr<NotifyHandlerServer> server;
  sync_completion_t received;
  auto client = NotifyHandlerServer::CreateAndServe(
      [&](uint32_t type, NotifyHandlerServer::HandleCompleter::Sync& completer) {
        ASSERT_EQ(type, 32);
        completers.emplace_back(completer.ToAsync());
        received_events++;
        sync_completion_signal(&received);
      },
      manager_.fidl_dispatcher(), &server);

  auto result = fidl_client_->InstallNotifyHandler(
      fuchsia_hardware_acpi::wire::NotificationMode::kSystem, std::move(client));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  zx_status_t status = ZX_OK;
  for (size_t i = 0; i < 2000; i++) {
    sync_completion_reset(&received);
    hnd->Notify(32);
    status = sync_completion_wait(&received, ZX_MSEC(500));
    if (status == ZX_ERR_TIMED_OUT) {
      break;
    }
  }

  // Should have eventually timed out.
  ASSERT_NE(status, ZX_OK);

  // Respond to the events.
  for (auto& completer : completers) {
    completer.Reply();
  }
  completers.clear();
}

TEST_F(AcpiDeviceTest, RemoveAndAddNotifyHandler) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());
  async::Loop server_loop(&kAsyncLoopConfigNeverAttachToThread);

  SetUpFidlServer(std::move(device));
  std::vector<NotifyHandlerServer::HandleCompleter::Async> completers;
  std::unique_ptr<NotifyHandlerServer> server;
  sync_completion_t received;
  auto handler = [&](uint32_t type, NotifyHandlerServer::HandleCompleter::Sync& completer) {
    completer.Reply();
    sync_completion_signal(&received);
  };

  {
    auto client = NotifyHandlerServer::CreateAndServe(handler, manager_.fidl_dispatcher(), &server);
    auto result = fidl_client_->InstallNotifyHandler(
        fuchsia_hardware_acpi::wire::NotificationMode::kSystem, std::move(client));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err(), "error %d", int(result->result.err()));
  }

  // Destroy the server, which will close the channel.
  server.reset();

  // Wait for the async close event to propagate.
  while (hnd->HasNotifyHandler()) {
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
  }

  // Try installing a new handler.
  {
    auto client = NotifyHandlerServer::CreateAndServe(handler, manager_.fidl_dispatcher(), &server);
    auto result = fidl_client_->InstallNotifyHandler(
        fuchsia_hardware_acpi::wire::NotificationMode::kSystem, std::move(client));
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());
  }

  hnd->Notify(32);
  sync_completion_wait(&received, ZX_TIME_INFINITE);
}

TEST_F(AcpiDeviceTest, ReceiveEventAfterUnbind) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(&manager_, mock_root_.get(), hnd, mock_root_.get());
  auto ptr = device.get();
  async::Loop server_loop(&kAsyncLoopConfigNeverAttachToThread);

  SetUpFidlServer(std::move(device));
  sync_completion_t done;
  std::unique_ptr<NotifyHandlerServer> server;
  auto client = NotifyHandlerServer::CreateAndServe(
      [&](uint32_t type, NotifyHandlerServer::HandleCompleter::Sync& completer) {
        ASSERT_EQ(type, 32);
        completer.Reply();
        sync_completion_signal(&done);
      },
      manager_.fidl_dispatcher(), &server);

  auto result = fidl_client_->InstallNotifyHandler(
      fuchsia_hardware_acpi::wire::NotificationMode::kSystem, std::move(client));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  device_async_remove(ptr->zxdev());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(mock_root_.get()));
  ASSERT_FALSE(hnd->HasNotifyHandler());
}
