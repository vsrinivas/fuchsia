// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/device.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

#include <zxtest/zxtest.h>

#include "fidl/fuchsia.hardware.acpi/cpp/markers.h"
#include "lib/ddk/device.h"
#include "src/devices/board/lib/acpi/manager-fuchsia.h"
#include "src/devices/board/lib/acpi/manager.h"
#include "src/devices/board/lib/acpi/test/device.h"
#include "src/devices/board/lib/acpi/test/mock-acpi.h"
#include "src/devices/board/lib/acpi/test/null-iommu-manager.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "third_party/acpica/source/include/actypes.h"

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

class AddressSpaceHandlerServer
    : public fidl::WireServer<fuchsia_hardware_acpi::AddressSpaceHandler> {
 public:
  ~AddressSpaceHandlerServer() override {
    if (ref_ != std::nullopt) {
      Close();
    }
  }
  static std::pair<std::unique_ptr<AddressSpaceHandlerServer>,
                   fidl::ClientEnd<fuchsia_hardware_acpi::AddressSpaceHandler>>
  CreateAndServe(async_dispatcher_t* dispatcher) {
    auto server = std::make_unique<AddressSpaceHandlerServer>();
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::AddressSpaceHandler>();
    server->ref_ = fidl::BindServer(dispatcher, std::move(endpoints->server), server.get());
    return std::pair(std::move(server), std::move(endpoints->client));
  }

  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override {
    uint64_t ret;
    switch (request->width) {
      case 8:
        ret = data_[request->address];
        break;
      case 16: {
        uint16_t val;
        memcpy(&val, &data_[request->address], sizeof(val));
        ret = val;
        break;
      }
      case 32: {
        uint32_t val;
        memcpy(&val, &data_[request->address], sizeof(val));
        ret = val;
        break;
      }
      case 64:
        memcpy(&ret, &data_[request->address], sizeof(ret));
        break;
      default:
        ZX_ASSERT(false);
    }

    completer.ReplySuccess(ret);
  }

  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override {
    switch (request->width) {
      case 8:
        data_[request->address] = request->value & UINT8_MAX;
        break;
      case 16: {
        uint16_t val = request->value & UINT16_MAX;
        memcpy(&data_[request->address], &val, sizeof(val));
        break;
      }
      case 32: {
        uint32_t val = request->value & UINT32_MAX;
        memcpy(&data_[request->address], &val, sizeof(val));
        break;
      }
      case 64:
        memcpy(&data_[request->address], &request->value, sizeof(request->value));
        break;
      default:
        ZX_ASSERT(false);
    }
    completer.ReplySuccess();
  }

  void Close() {
    ref_->Close(ZX_ERR_PEER_CLOSED);
    ref_ = std::nullopt;
  }

  std::vector<uint8_t> data_;

 private:
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_acpi::AddressSpaceHandler>> ref_;
};

class AcpiDeviceTest : public zxtest::Test {
 public:
  AcpiDeviceTest()
      : mock_root_(MockDevice::FakeRootParent()), manager_(&acpi_, &iommu_, mock_root_.get()) {}

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

  zx_device_t* HandOffToDdk(std::unique_ptr<acpi::Device> device) {
    EXPECT_OK(device
                  ->AddDevice("test-acpi-device", cpp20::span<zx_device_prop_t>(),
                              cpp20::span<zx_device_str_prop_t>(), 0)
                  .status_value());

    // Give mock_ddk ownership of the device.
    zx_device_t* dev = device.release()->zxdev();
    dev->InitOp();
    dev->WaitUntilInitReplyCalled(zx::time::infinite());
    return dev;
  }

  void SetUpFidlServer(std::unique_ptr<acpi::Device> device) {
    zx_device_t* dev = HandOffToDdk(std::move(device));

    // Bind FIDL device.
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::Device>();
    ASSERT_OK(endpoints.status_value());

    fidl::BindServer(manager_.fidl_dispatcher(), std::move(endpoints->server),
                     dev->GetDeviceContext<acpi::Device>());
    fidl_client_.Bind(std::move(endpoints->client));
  }

  acpi::DeviceArgs Args(void* handle) {
    return acpi::DeviceArgs(mock_root_.get(), &manager_, handle);
  }

  ACPI_HANDLE AddPowerResource(const std::string& name, uint8_t system_level,
                               uint16_t resource_order) {
    auto power_resource = std::make_unique<acpi::test::Device>(name);
    power_resource->SetPowerResourceMethods(system_level, resource_order);
    ACPI_HANDLE handle = power_resource.get();
    acpi_.GetDeviceRoot()->AddChild(std::move(power_resource));
    return handle;
  }

 protected:
  std::shared_ptr<MockDevice> mock_root_;
  acpi::FuchsiaManager manager_;
  acpi::test::MockAcpi acpi_;
  NullIommuManager iommu_;
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> fidl_client_;
};

TEST_F(AcpiDeviceTest, TestGetBusId) {
  auto device = std::make_unique<acpi::Device>(std::move(
      Args(ACPI_ROOT_OBJECT).SetBusMetadata(std::vector<uint8_t>(), acpi::BusType::kI2c, 37)));
  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->GetBusId();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_EQ(result->value()->bus_id, 37);
}

TEST_F(AcpiDeviceTest, TestAcquireGlobalLockAccessDenied) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->AcquireGlobalLock();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kAccess);
}

// _GLK method exists, but returns zero.
TEST_F(AcpiDeviceTest, TestAcquireGlobalLockAccessDeniedButMethodExists) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  test_dev->SetGlk(false);
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  SetUpFidlServer(std::move(device));

  auto result = fidl_client_->AcquireGlobalLock();
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kAccess);
}

TEST_F(AcpiDeviceTest, TestAcquireGlobalLockImplicitRelease) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  test_dev->SetGlk(true);
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  SetUpFidlServer(std::move(device));

  sync_completion_t acquired;
  sync_completion_t running;
  {
    auto result = fidl_client_->AcquireGlobalLock();
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->is_ok(), "ACPI error %d", static_cast<uint32_t>(result->error_value()));

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
  auto device = std::make_unique<acpi::Device>(Args(hnd));
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
  ASSERT_FALSE(result->is_error());

  hnd->Notify(32);
  sync_completion_wait(&done, ZX_TIME_INFINITE);
}

TEST_F(AcpiDeviceTest, TestNotifyHandlerDropsEvents) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(Args(hnd));
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
  ASSERT_FALSE(result->is_error());

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
  auto device = std::make_unique<acpi::Device>(Args(hnd));
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
    ASSERT_FALSE(result->is_error(), "error %d", static_cast<uint32_t>(result->error_value()));
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
    ASSERT_FALSE(result->is_error());
  }

  hnd->Notify(32);
  sync_completion_wait(&received, ZX_TIME_INFINITE);
}

TEST_F(AcpiDeviceTest, ReceiveEventAfterUnbind) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(Args(hnd));
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
  ASSERT_FALSE(result->is_error());

  device_async_remove(ptr->zxdev());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(mock_root_.get()));
  ASSERT_FALSE(hnd->HasNotifyHandler());
}

TEST_F(AcpiDeviceTest, TestRemoveNotifyHandler) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));
  auto device = std::make_unique<acpi::Device>(Args(hnd));
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
  ASSERT_FALSE(result->is_error());
  ASSERT_TRUE(hnd->HasNotifyHandler());

  (void)fidl_client_->RemoveNotifyHandler();
  ASSERT_FALSE(hnd->HasNotifyHandler());
}

TEST_F(AcpiDeviceTest, TestAddressHandlerInstall) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  SetUpFidlServer(std::move(device));

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::AddressSpaceHandler>();
  ASSERT_OK(endpoints.status_value());

  auto [server, client] = AddressSpaceHandlerServer::CreateAndServe(manager_.fidl_dispatcher());

  auto result = fidl_client_->InstallAddressSpaceHandler(
      fuchsia_hardware_acpi::wire::AddressSpace::kEc, std::move(client));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
}

TEST_F(AcpiDeviceTest, TestAddressHandlerReadWrite) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  SetUpFidlServer(std::move(device));

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_acpi::AddressSpaceHandler>();
  ASSERT_OK(endpoints.status_value());

  auto [server, client] = AddressSpaceHandlerServer::CreateAndServe(manager_.fidl_dispatcher());

  auto result = fidl_client_->InstallAddressSpaceHandler(
      fuchsia_hardware_acpi::wire::AddressSpace::kEc, std::move(client));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());

  server->data_.resize(256, 0);
  UINT64 value = 0xff;
  ASSERT_EQ(hnd->AddressSpaceOp(ACPI_ADR_SPACE_EC, ACPI_READ, 0, 64, &value).status_value(), AE_OK);
  ASSERT_EQ(value, 0);
  value = 0xdeadbeefd00dfeed;
  ASSERT_EQ(hnd->AddressSpaceOp(ACPI_ADR_SPACE_EC, ACPI_WRITE, 0, 64, &value).status_value(),
            AE_OK);
  value = 0;
  ASSERT_EQ(hnd->AddressSpaceOp(ACPI_ADR_SPACE_EC, ACPI_READ, 0, 64, &value).status_value(), AE_OK);
  ASSERT_EQ(value, 0xdeadbeefd00dfeed);
}

TEST_F(AcpiDeviceTest, TestInitializePowerManagementNoSupportedStates) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");
  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  std::unordered_map<uint8_t, acpi::DevicePowerState> states =
      acpi_device->GetSupportedPowerStates();
  ASSERT_EQ(states.size(), 0);
}

TEST_F(AcpiDeviceTest, TestInitializePowerManagementPowerResources) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 1, 0);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 2, 0);
  ACPI_HANDLE power_resource_handle3 = AddPowerResource("POW3", 3, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");
  acpi::test::Device* mock_power_device3 = acpi_.GetDeviceRoot()->FindByPath("\\POW3");

  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  test_dev->AddMethodCallback("_PR0", [power_resource_handle1, power_resource_handle2](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_PR1", [power_resource_handle1, power_resource_handle3](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle3}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_PR2", [power_resource_handle2, power_resource_handle3](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle3}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback(
      "_PR3", [power_resource_handle3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 1> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle3}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  std::unordered_map<uint8_t, acpi::DevicePowerState> states =
      acpi_device->GetSupportedPowerStates();
  ASSERT_EQ(states.size(), 5);
  ASSERT_EQ(states.find(DEV_POWER_STATE_D0)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D1)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D2)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D3HOT)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3}));
  // Power resources are declared for D3HOT, so D3COLD is supported.
  ASSERT_EQ(states.find(DEV_POWER_STATE_D3COLD)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3, 4}));

  // Test that the device was initially transitioned to D0 by making sure only the power resources
  // required for D0 were turned on.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 0);
}

TEST_F(AcpiDeviceTest, TestInitializePowerManagementPowerResourceOrder) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 1, 2);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 2, 1);
  ACPI_HANDLE power_resource_handle3 = AddPowerResource("POW3", 3, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");
  acpi::test::Device* mock_power_device3 = acpi_.GetDeviceRoot()->FindByPath("\\POW3");

  mock_power_device1->AddMethodCallback(
      "_ON", [mock_power_device1, mock_power_device2,
              mock_power_device3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        // Make sure power resources with lower system orders are already on.
        EXPECT_EQ(mock_power_device2->sta(), 1);
        EXPECT_EQ(mock_power_device3->sta(), 1);
        mock_power_device1->SetSta(1);
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
      });

  mock_power_device2->AddMethodCallback("_ON", [mock_power_device2, mock_power_device3](
                                                   const std::optional<std::vector<ACPI_OBJECT>>&) {
    // Make sure power resources with lower system orders are already on.
    EXPECT_EQ(mock_power_device3->sta(), 1);
    mock_power_device2->SetSta(1);
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  mock_power_device1->AddMethodCallback(
      "_OFF", [mock_power_device1, mock_power_device2,
               mock_power_device3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        // Make sure power resources with lower system orders are still on.
        EXPECT_EQ(mock_power_device2->sta(), 1);
        EXPECT_EQ(mock_power_device3->sta(), 1);
        mock_power_device1->SetSta(0);
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
      });

  mock_power_device2->AddMethodCallback(
      "_OFF",
      [mock_power_device2, mock_power_device3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        // Make sure power resources with lower system orders are still on.
        EXPECT_EQ(mock_power_device3->sta(), 1);
        mock_power_device2->SetSta(0);
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
      });

  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  test_dev->AddMethodCallback(
      "_PR0", [power_resource_handle1, power_resource_handle2,
               power_resource_handle3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 3> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle2}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle3}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  test_dev->AddMethodCallback(
      "_PR3", [power_resource_handle1, power_resource_handle2,
               power_resource_handle3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 3> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle2}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle3}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  // Test that the device was initially transitioned to D0 by making sure the power resources
  // required for D0 were turned on.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);

  // Suspend the device to make sure power resources are turned off in the right order.
  acpi::PowerStateTransitionResponse result =
      acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);
  // Make sure the power resources were turned off.
  ASSERT_EQ(mock_power_device1->sta(), 0);
  ASSERT_EQ(mock_power_device2->sta(), 0);
  ASSERT_EQ(mock_power_device3->sta(), 0);

  // Resume the device again to make sure power resources are turned on in the right order.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  // Make sure the power resources were turned on.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);
}

TEST_F(AcpiDeviceTest, TestInitializePowerManagementPsxMethods) {
  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  bool ps0_called = false;
  test_dev->AddMethodCallback("_PS0",
                              [&ps0_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps0_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps1_called = false;
  test_dev->AddMethodCallback("_PS1",
                              [&ps1_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps1_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps2_called = false;
  test_dev->AddMethodCallback("_PS2",
                              [&ps2_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps2_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps3_called = false;
  test_dev->AddMethodCallback("_PS3",
                              [&ps3_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps3_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  test_dev->AddMethodCallback("_S1D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 1;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_S2D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 2;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_S3D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 2;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_S4D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 3;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  std::unordered_map<uint8_t, acpi::DevicePowerState> states =
      acpi_device->GetSupportedPowerStates();
  ASSERT_EQ(states.size(), 4);
  ASSERT_EQ(states.find(DEV_POWER_STATE_D0)->second.supported_s_states,
            std::unordered_set<uint8_t>({0}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D1)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D2)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3}));
  // Power resources are not declared for D3HOT, so D3COLD is not supported.
  ASSERT_EQ(states.find(DEV_POWER_STATE_D3HOT)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3, 4}));

  // Test that the device was initially transitioned to D0.
  ASSERT_TRUE(ps0_called);
  ASSERT_FALSE(ps1_called);
  ASSERT_FALSE(ps2_called);
  ASSERT_FALSE(ps3_called);
}

TEST_F(AcpiDeviceTest, TestInitializePowerManagementPowerResourcesAndPsxMethods) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 3, 0);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 4, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");

  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  test_dev->AddMethodCallback("_PR0", [power_resource_handle1, power_resource_handle2](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_PR3", [power_resource_handle1, power_resource_handle2](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  bool ps0_called = false;
  test_dev->AddMethodCallback("_PS0", [&ps0_called, mock_power_device1, mock_power_device2](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    // Make sure power resources were turned on BEFORE calling PS0.
    EXPECT_EQ(mock_power_device1->sta(), 1);
    EXPECT_EQ(mock_power_device2->sta(), 1);
    ps0_called = true;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  bool ps3_called = false;
  test_dev->AddMethodCallback("_PS3",
                              [&ps3_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps3_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  test_dev->AddMethodCallback("_S1D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 3;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback("_S3D", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    retval->Integer.Value = 3;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  std::unordered_map<uint8_t, acpi::DevicePowerState> states =
      acpi_device->GetSupportedPowerStates();
  ASSERT_EQ(states.size(), 3);
  ASSERT_EQ(states.find(DEV_POWER_STATE_D0)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 2}));
  ASSERT_EQ(states.find(DEV_POWER_STATE_D3HOT)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3}));
  // Power resources are declared for D3HOT, so D3COLD is supported.
  ASSERT_EQ(states.find(DEV_POWER_STATE_D3COLD)->second.supported_s_states,
            std::unordered_set<uint8_t>({0, 1, 2, 3, 4}));

  // Test that the device was initially transitioned to D0.
  ASSERT_TRUE(ps0_called);
  ASSERT_FALSE(ps3_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
}

TEST_F(AcpiDeviceTest, TestTransitioningBetweenPowerStates) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 0, 0);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 0, 0);
  ACPI_HANDLE power_resource_handle3 = AddPowerResource("POW3", 0, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");
  acpi::test::Device* mock_power_device3 = acpi_.GetDeviceRoot()->FindByPath("\\POW3");

  // Turn the power resources for D0 on initially.
  mock_power_device1->SetSta(1);
  mock_power_device2->SetSta(1);
  mock_power_device3->SetSta(1);

  bool power_resource1_on_called = false;
  mock_power_device1->AddMethodCallback("_ON", [mock_power_device1, &power_resource1_on_called](
                                                   const std::optional<std::vector<ACPI_OBJECT>>&) {
    power_resource1_on_called = true;
    mock_power_device1->SetSta(1);
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  bool power_resource2_on_called = false;
  mock_power_device2->AddMethodCallback("_ON", [mock_power_device2, &power_resource2_on_called](
                                                   const std::optional<std::vector<ACPI_OBJECT>>&) {
    power_resource2_on_called = true;
    mock_power_device2->SetSta(1);
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  bool power_resource3_on_called = false;
  mock_power_device3->AddMethodCallback("_ON", [mock_power_device3, &power_resource3_on_called](
                                                   const std::optional<std::vector<ACPI_OBJECT>>&) {
    power_resource3_on_called = true;
    mock_power_device3->SetSta(1);
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  test_dev->AddMethodCallback(
      "_PR0", [power_resource_handle1, power_resource_handle2,
               power_resource_handle3](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 3> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle2}},
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle3}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  test_dev->AddMethodCallback("_PR1", [power_resource_handle1, power_resource_handle2](
                                          const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev->AddMethodCallback(
      "_PR3", [power_resource_handle1](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 1> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  bool ps0_called = false;
  test_dev->AddMethodCallback("_PS0",
                              [&ps0_called, mock_power_device1, mock_power_device2,
                               mock_power_device3](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                // Make sure power resources were turned on BEFORE calling PS0.
                                EXPECT_EQ(mock_power_device1->sta(), 1);
                                EXPECT_EQ(mock_power_device2->sta(), 1);
                                EXPECT_EQ(mock_power_device3->sta(), 1);
                                ps0_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps1_called = false;
  test_dev->AddMethodCallback("_PS1",
                              [&ps1_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps1_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps3_called = false;
  test_dev->AddMethodCallback("_PS3",
                              [&ps3_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps3_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  zx_device_t* dev = HandOffToDdk(std::move(device));
  acpi::Device* acpi_device = dev->GetDeviceContext<acpi::Device>();

  std::unordered_map<uint8_t, acpi::DevicePowerState> states =
      acpi_device->GetSupportedPowerStates();
  ASSERT_EQ(states.size(), 4);
  ASSERT_EQ(states.count(DEV_POWER_STATE_D0), 1);
  ASSERT_EQ(states.count(DEV_POWER_STATE_D1), 1);
  ASSERT_EQ(states.count(DEV_POWER_STATE_D3HOT), 1);
  // Power resources are declared for D3HOT, so D3COLD is supported.
  ASSERT_EQ(states.count(DEV_POWER_STATE_D3COLD), 1);

  // _PS0 should be called even though the device is inferred from power resources to be in D0
  // initially.
  ASSERT_TRUE(ps0_called);
  ps0_called = false;
  ASSERT_FALSE(ps1_called);
  ASSERT_FALSE(ps3_called);
  // _ON should be called for D0 power resources even though they start on.
  ASSERT_TRUE(power_resource1_on_called);
  ASSERT_TRUE(power_resource2_on_called);
  ASSERT_TRUE(power_resource3_on_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);

  acpi::PowerStateTransitionResponse result =
      acpi_device->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  // Nothing should happen, the device was already in D0.
  ASSERT_FALSE(ps0_called);
  ASSERT_FALSE(ps1_called);
  ASSERT_FALSE(ps3_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);

  // D2 is not a supported state.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D2);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);

  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D1);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D1);
  ASSERT_FALSE(ps0_called);
  ASSERT_TRUE(ps1_called);
  ps1_called = false;
  ASSERT_FALSE(ps3_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 0);

  // Can't transition from D1 to D3hot.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3HOT);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D1);

  // Can't transition from D1 to D3cold.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D1);

  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  ASSERT_TRUE(ps0_called);
  ps0_called = false;
  ASSERT_FALSE(ps1_called);
  ASSERT_FALSE(ps3_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);

  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3HOT);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3HOT);
  ASSERT_FALSE(ps0_called);
  ASSERT_FALSE(ps1_called);
  ASSERT_TRUE(ps3_called);
  ps3_called = false;
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 0);
  ASSERT_EQ(mock_power_device3->sta(), 0);

  // Can't transition from D3hot to D1.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D1);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3HOT);

  // Transition from D3hot to D3cold.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);
  ASSERT_FALSE(ps0_called);
  ASSERT_FALSE(ps1_called);
  // PS3 was already called in the transition to D3hot.
  ASSERT_FALSE(ps3_called);
  // No power resources are on.
  ASSERT_EQ(mock_power_device1->sta(), 0);
  ASSERT_EQ(mock_power_device2->sta(), 0);
  ASSERT_EQ(mock_power_device3->sta(), 0);

  // Can't transition from D3cold to D1.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D1);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);

  // Can't transition from D3cold to D3hot.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3HOT);
  ASSERT_STATUS(result.status, ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);

  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  ASSERT_TRUE(ps0_called);
  ps0_called = false;
  ASSERT_FALSE(ps1_called);
  ASSERT_FALSE(ps3_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
  ASSERT_EQ(mock_power_device3->sta(), 1);

  // Transition all the way from D0 to D3cold.
  result = acpi_device->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);
  ASSERT_FALSE(ps0_called);
  ASSERT_FALSE(ps1_called);
  ASSERT_TRUE(ps3_called);
  ps3_called = false;
  ASSERT_EQ(mock_power_device1->sta(), 0);
  ASSERT_EQ(mock_power_device2->sta(), 0);
  ASSERT_EQ(mock_power_device3->sta(), 0);
}

TEST_F(AcpiDeviceTest, TestPscMethod) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 0, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");

  // Turn the power resources for D0 on initially.
  mock_power_device1->SetSta(1);

  bool power_resource1_on_called = false;
  mock_power_device1->AddMethodCallback("_ON", [mock_power_device1, &power_resource1_on_called](
                                                   const std::optional<std::vector<ACPI_OBJECT>>&) {
    power_resource1_on_called = true;
    mock_power_device1->SetSta(1);
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
  });

  auto test_dev = std::make_unique<acpi::test::Device>("TEST");

  test_dev->AddMethodCallback(
      "_PR0", [power_resource_handle1](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 1> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  test_dev->AddMethodCallback(
      "_PR3", [power_resource_handle1](const std::optional<std::vector<ACPI_OBJECT>>&) {
        static std::array<ACPI_OBJECT, 1> power_resources{
            ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                      .ActualType = ACPI_TYPE_POWER,
                                      .Handle = power_resource_handle1}}};

        ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
        retval->Package.Type = ACPI_TYPE_PACKAGE;
        retval->Package.Count = power_resources.size();
        retval->Package.Elements = power_resources.data();
        return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
      });

  bool ps0_called = false;
  test_dev->AddMethodCallback("_PS0",
                              [&ps0_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps0_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  bool ps3_called = false;
  test_dev->AddMethodCallback("_PS3",
                              [&ps3_called](const std::optional<std::vector<ACPI_OBJECT>>&) {
                                ps3_called = true;
                                return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>());
                              });

  test_dev->AddMethodCallback("_PSC", [](const std::optional<std::vector<ACPI_OBJECT>>&) {
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Integer.Type = ACPI_TYPE_INTEGER;
    // The device starts at D0.
    retval->Integer.Value = 0;
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi::test::Device* hnd = test_dev.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev));

  auto device = std::make_unique<acpi::Device>(Args(hnd));

  HandOffToDdk(std::move(device));

  // _PS0 should not be called as _PSC explicitly states that the device starts at D0.
  ASSERT_FALSE(ps0_called);
  ASSERT_FALSE(ps3_called);
  // _ON should be called for D0 power resources even though they start on.
  ASSERT_TRUE(power_resource1_on_called);
  ASSERT_EQ(mock_power_device1->sta(), 1);
}

TEST_F(AcpiDeviceTest, TestSharedPowerResources) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 0, 0);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 0, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");

  auto test_dev1 = std::make_unique<acpi::test::Device>("TST1");
  auto test_dev2 = std::make_unique<acpi::test::Device>("TST2");

  test_dev1->AddMethodCallback("_PR0", [power_resource_handle1, power_resource_handle2](
                                           const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev2->AddMethodCallback("_PR0", [power_resource_handle1, power_resource_handle2](
                                           const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev1->AddMethodCallback("_PR3", [power_resource_handle1, power_resource_handle2](
                                           const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  test_dev2->AddMethodCallback("_PR3", [power_resource_handle1, power_resource_handle2](
                                           const std::optional<std::vector<ACPI_OBJECT>>&) {
    static std::array<ACPI_OBJECT, 2> power_resources{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = power_resources.size();
    retval->Package.Elements = power_resources.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi::test::Device* hnd = test_dev1.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev1));
  auto device1 = std::make_unique<acpi::Device>(Args(hnd));
  zx_device_t* dev1 = HandOffToDdk(std::move(device1));
  acpi::Device* acpi_device1 = dev1->GetDeviceContext<acpi::Device>();

  // The power resources should now be on with just one device initialized.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);

  hnd = test_dev2.get();
  acpi_.GetDeviceRoot()->AddChild(std::move(test_dev2));
  auto device2 = std::make_unique<acpi::Device>(Args(hnd));
  zx_device_t* dev2 = HandOffToDdk(std::move(device2));
  acpi::Device* acpi_device2 = dev2->GetDeviceContext<acpi::Device>();

  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);

  acpi::PowerStateTransitionResponse result =
      acpi_device1->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);
  // TST2 is still using these power resources.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);

  result = acpi_device2->TransitionToPowerState(DEV_POWER_STATE_D3HOT);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3HOT);
  // TST2 is still using these power resources.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);

  result = acpi_device2->TransitionToPowerState(DEV_POWER_STATE_D3COLD);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D3COLD);
  // Now no device is using the power resources.
  ASSERT_EQ(mock_power_device1->sta(), 0);
  ASSERT_EQ(mock_power_device2->sta(), 0);

  result = acpi_device1->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  // TST1 is using these power resources now.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);

  result = acpi_device2->TransitionToPowerState(DEV_POWER_STATE_D0);
  ASSERT_OK(result.status);
  ASSERT_EQ(result.out_state, DEV_POWER_STATE_D0);
  // TST1 and TST2 are using these power resources now.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceFadt) {
  auto wake_dev1 = std::make_unique<acpi::test::Device>("WAK1");
  auto wake_dev2 = std::make_unique<acpi::test::Device>("WAK2");

  ACPI_HANDLE hnd2 = wake_dev2.get();

  wake_dev1->AddMethodCallback("_PRW", [](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> objects = {
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 24}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  wake_dev2->AddMethodCallback("_PRW", [](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> objects = {
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 25}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev1));
  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev2));

  // Wake device 2 is the one we will set as a wake source
  SetUpFidlServer(std::make_unique<acpi::Device>(Args(hnd2)));

  // Check that both devices are recognised as potential wake sources, but that none are
  // currently set.
  ASSERT_EQ(AE_OK, acpi_.DiscoverWakeGpes().status_value());
  auto& wake_gpes = acpi_.GetWakeGpes();
  ASSERT_EQ(2, wake_gpes.size());
  for (acpi::test::WakeGpe gpe : wake_gpes) {
    ASSERT_FALSE(gpe.enabled);
  }

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());

  // Check that only device 2 is set as a wake source.
  for (acpi::test::WakeGpe gpe : wake_gpes) {
    if ((gpe.gpe_dev == nullptr) && (gpe.gpe_num == 25)) {
      ASSERT_TRUE(gpe.enabled);
    } else {
      ASSERT_FALSE(gpe.enabled);
    }
  }
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceBlockDevice) {
  auto wake_dev1 = std::make_unique<acpi::test::Device>("WAK1");
  wake_dev1->SetHid("ACPI0006");
  auto wake_dev2 = std::make_unique<acpi::test::Device>("WAK2");
  wake_dev2->SetHid("ACPI0006");

  ACPI_HANDLE hnd1 = wake_dev1.get();
  ACPI_HANDLE hnd2 = wake_dev2.get();

  auto wake_dev1_ref_gpe = std::make_unique<acpi::test::Device>("RWG1");
  ACPI_HANDLE wake_dev1_ref_hnd = wake_dev1_ref_gpe.get();
  wake_dev1_ref_gpe->AddMethodCallback("_PRW", [hnd1](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> gpe_objects{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_DEVICE,
                                  .Handle = hnd1}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 76}}};
    static std::array<ACPI_OBJECT, 2> prw_objects{
        ACPI_OBJECT{.Package = {.Type = ACPI_TYPE_PACKAGE,
                                .Count = gpe_objects.size(),
                                .Elements = gpe_objects.data()}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  auto wake_dev2_ref_gpe = std::make_unique<acpi::test::Device>("RWG2");
  wake_dev2_ref_gpe->AddMethodCallback("_PRW", [hnd2](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> gpe_objects{
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_DEVICE,
                                  .Handle = hnd2}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 77}}};
    static std::array<ACPI_OBJECT, 2> prw_objects{
        ACPI_OBJECT{.Package = {.Type = ACPI_TYPE_PACKAGE,
                                .Count = gpe_objects.size(),
                                .Elements = gpe_objects.data()}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev1));
  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev1_ref_gpe));
  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev2));
  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev2_ref_gpe));

  // Wake device 1 is the one we will set as a wake source
  SetUpFidlServer(std::make_unique<acpi::Device>(Args(wake_dev1_ref_hnd)));

  // Check that both devices are recognised as potential wake sources, but that none are
  // currently set.
  ASSERT_EQ(AE_OK, acpi_.DiscoverWakeGpes().status_value());
  auto& wake_gpes = acpi_.GetWakeGpes();
  ASSERT_EQ(2, wake_gpes.size());
  for (acpi::test::WakeGpe gpe : wake_gpes) {
    ASSERT_FALSE(gpe.enabled);
  }

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok(), "ACPI error %d", static_cast<uint32_t>(result->error_value()));

  // Check that only device 1 is set as a wake source.
  for (acpi::test::WakeGpe gpe : wake_gpes) {
    if ((gpe.gpe_dev == hnd1) && (gpe.gpe_num == 76)) {
      ASSERT_TRUE(gpe.enabled);
    } else {
      ASSERT_FALSE(gpe.enabled);
    }
  }
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceWithPowerResources) {
  ACPI_HANDLE power_resource_handle1 = AddPowerResource("POW1", 0, 0);
  ACPI_HANDLE power_resource_handle2 = AddPowerResource("POW2", 0, 0);
  acpi::test::Device* mock_power_device1 = acpi_.GetDeviceRoot()->FindByPath("\\POW1");
  acpi::test::Device* mock_power_device2 = acpi_.GetDeviceRoot()->FindByPath("\\POW2");

  auto wake_dev = std::make_unique<acpi::test::Device>("WAKD");

  ACPI_HANDLE hnd = wake_dev.get();

  wake_dev->AddMethodCallback("_PRW", [power_resource_handle1, power_resource_handle2](
                                          std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 4> objects = {
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 24}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle1}},
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_POWER,
                                  .Handle = power_resource_handle2}}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev));

  SetUpFidlServer(std::make_unique<acpi::Device>(Args(hnd)));

  ASSERT_EQ(AE_OK, acpi_.DiscoverWakeGpes().status_value());
  auto& wake_gpes = acpi_.GetWakeGpes();
  ASSERT_EQ(1, wake_gpes.size());
  auto& gpe = wake_gpes[0];
  ASSERT_FALSE(gpe.enabled);

  // Power resources are off.
  ASSERT_EQ(mock_power_device1->sta(), 0);
  ASSERT_EQ(mock_power_device2->sta(), 0);

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->is_ok());
  ASSERT_TRUE(gpe.enabled);

  // Power resources are on.
  ASSERT_EQ(mock_power_device1->sta(), 1);
  ASSERT_EQ(mock_power_device2->sta(), 1);
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceUnsupportedSleepState) {
  auto wake_dev = std::make_unique<acpi::test::Device>("WAKD");

  ACPI_HANDLE hnd = wake_dev.get();

  wake_dev->AddMethodCallback("_PRW", [](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> objects = {
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 24}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 3}}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(wake_dev));

  SetUpFidlServer(std::make_unique<acpi::Device>(Args(hnd)));

  auto result = fidl_client_->SetWakeDevice(4);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kNotSupported);
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceWrongObjectSize) {
  auto non_wake_dev = std::make_unique<acpi::test::Device>("NWDV");

  ACPI_HANDLE hnd = non_wake_dev.get();

  non_wake_dev->AddMethodCallback("_PRW", [hnd](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 1> prw_objects{ACPI_OBJECT{
        .Reference = {
            .Type = ACPI_TYPE_LOCAL_REFERENCE, .ActualType = ACPI_TYPE_POWER, .Handle = hnd}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(non_wake_dev));

  // Wake device 1 is the one we will set as a wake source
  SetUpFidlServer(std::make_unique<acpi::Device>(Args(hnd)));

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kBadData);
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceWrongEventInfo) {
  auto non_wake_dev = std::make_unique<acpi::test::Device>("NWD1");
  non_wake_dev->SetHid("ACPI0006");

  ACPI_HANDLE hnd = non_wake_dev.get();

  auto dev_ref_gpe = std::make_unique<acpi::test::Device>("RWG1");
  ACPI_HANDLE wake_dev_ref_hnd = dev_ref_gpe.get();
  dev_ref_gpe->AddMethodCallback("_PRW", [hnd](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 1> gpe_objects{ACPI_OBJECT{
        .Reference = {
            .Type = ACPI_TYPE_LOCAL_REFERENCE, .ActualType = ACPI_TYPE_DEVICE, .Handle = hnd}}};
    static std::array<ACPI_OBJECT, 2> prw_objects{
        ACPI_OBJECT{.Package = {.Type = ACPI_TYPE_PACKAGE,
                                .Count = gpe_objects.size(),
                                .Elements = gpe_objects.data()}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};

    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = prw_objects.size();
    retval->Package.Elements = prw_objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(non_wake_dev));
  acpi_.GetDeviceRoot()->AddChild(std::move(dev_ref_gpe));

  // Wake device 1 is the one we will set as a wake source
  SetUpFidlServer(std::make_unique<acpi::Device>(Args(wake_dev_ref_hnd)));

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kBadData);
}

TEST_F(AcpiDeviceTest, TestSetWakeDeviceWrongEventInfoType) {
  auto non_wake_dev = std::make_unique<acpi::test::Device>("NWDV");

  ACPI_HANDLE hnd = non_wake_dev.get();

  non_wake_dev->AddMethodCallback("_PRW", [hnd](std::optional<std::vector<ACPI_OBJECT>>) {
    static std::array<ACPI_OBJECT, 2> objects = {
        ACPI_OBJECT{.Reference = {.Type = ACPI_TYPE_LOCAL_REFERENCE,
                                  .ActualType = ACPI_TYPE_DEVICE,
                                  .Handle = hnd}},
        ACPI_OBJECT{.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 4}}};
    ACPI_OBJECT* retval = static_cast<ACPI_OBJECT*>(AcpiOsAllocate(sizeof(*retval)));
    retval->Package.Type = ACPI_TYPE_PACKAGE;
    retval->Package.Count = objects.size();
    retval->Package.Elements = objects.data();
    return acpi::ok(acpi::UniquePtr<ACPI_OBJECT>(retval));
  });

  acpi_.GetDeviceRoot()->AddChild(std::move(non_wake_dev));

  // Wake device 1 is the one we will set as a wake source
  SetUpFidlServer(std::make_unique<acpi::Device>(Args(hnd)));

  auto result = fidl_client_->SetWakeDevice(3);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_hardware_acpi::wire::Status::kBadData);
}
