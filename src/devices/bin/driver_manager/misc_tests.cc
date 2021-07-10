// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/test/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>

#include <vector>

#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "coordinator_test_utils.h"
#include "devfs.h"
#include "driver_host.h"
#include "driver_test_reporter.h"
#include "fdio.h"

constexpr char kDriverPath[] = "/pkg/driver/test/mock-device.so";
constexpr char kLogMessage[] = "log message text";
constexpr char kLogTestCaseName[] = "log test case";

static CoordinatorConfig NullConfig() { return DefaultConfig(nullptr, nullptr, nullptr); }

namespace {

class FidlTransaction : public fidl::Transaction {
 public:
  FidlTransaction(FidlTransaction&&) = default;
  explicit FidlTransaction(zx_txid_t transaction_id, zx::unowned_channel channel)
      : txid_(transaction_id), channel_(channel) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override {
    return std::make_unique<FidlTransaction>(std::move(*this));
  }

  zx_status_t Reply(fidl::OutgoingMessage* message) override {
    ZX_ASSERT(txid_ != 0);
    message->set_txid(txid_);
    txid_ = 0;
    message->Write(channel_);
    return message->status();
  }

  void Close(zx_status_t epitaph) override { ZX_ASSERT(false); }

  ~FidlTransaction() override = default;

 private:
  zx_txid_t txid_;
  zx::unowned_channel channel_;
};

class FakeDevice : public fidl::WireServer<fuchsia_device_manager::DeviceController> {
 public:
  FakeDevice(zx::channel test_output, const char* expected_driver = nullptr)
      : test_output_(std::move(test_output)), expected_driver_(expected_driver) {}

  void BindDriver(BindDriverRequestView request, BindDriverCompleter::Sync& completer) override {
    if (expected_driver_ == nullptr ||
        strncmp(expected_driver_, request->driver_path.data(), request->driver_path.size()) == 0) {
      bind_called_ = true;
      completer.Reply(ZX_OK, std::move(test_output_));
      return;
    }
    completer.Reply(ZX_ERR_INTERNAL, zx::channel{});
  }
  void ConnectProxy(ConnectProxyRequestView request,
                    ConnectProxyCompleter::Sync& _completer) override {}
  void Init(InitRequestView request, InitCompleter::Sync& completer) override {}
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override {}
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& completer) override {}
  void Unbind(UnbindRequestView request, UnbindCompleter::Sync& completer) override {}
  void CompleteRemoval(CompleteRemovalRequestView request,
                       CompleteRemovalCompleter::Sync& completer) override {}
  void CompleteCompatibilityTests(CompleteCompatibilityTestsRequestView request,
                                  CompleteCompatibilityTestsCompleter::Sync& _completer) override {}
  void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override {}

  bool bind_called() { return bind_called_; }

 private:
  zx::channel test_output_;
  const char* expected_driver_;
  bool bind_called_ = false;
};

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void BindDriverTestOutput(
    const fidl::ServerEnd<fuchsia_device_manager::DeviceController>& controller,
    zx::channel test_output) {
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::IncomingMessage msg =
      fidl::ChannelReadEtc(controller.channel().get(), 0, fidl::BufferSpan(bytes, std::size(bytes)),
                           cpp20::span(handles));
  ASSERT_TRUE(msg.ok());

  auto* header = msg.header();
  FidlTransaction txn(header->txid, zx::unowned(controller.channel()));

  FakeDevice fake(std::move(test_output));
  ASSERT_EQ(fidl::WireDispatch(
                static_cast<fidl::WireServer<fuchsia_device_manager::DeviceController>*>(&fake),
                std::move(msg), &txn),
            fidl::DispatchResult::kFound);
  ASSERT_TRUE(fake.bind_called());
}

void WriteTestLog(const zx::channel& output) {
  uint32_t len =
      sizeof(fuchsia_driver_test_LoggerLogMessageRequest) + FIDL_ALIGN(strlen(kLogMessage));
  FIDL_ALIGNDECL uint8_t bytes[len];
  fidl::Builder builder(bytes, len);

  auto* req = builder.New<fuchsia_driver_test_LoggerLogMessageRequest>();
  fidl_init_txn_header(&req->hdr, FIDL_TXID_NO_RESPONSE,
                       fuchsia_driver_test_LoggerLogMessageOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(strlen(kLogMessage)));
  req->msg.data = data;
  req->msg.size = strlen(kLogMessage);
  memcpy(data, kLogMessage, strlen(kLogMessage));

  fidl::HLCPPOutgoingMessage msg(builder.Finalize(), fidl::HandleDispositionPart());
  const char* err = nullptr;
  zx_status_t status = msg.Encode(&fuchsia_driver_test_LoggerLogMessageRequestTable, &err);
  ASSERT_OK(status);
  status = msg.Write(output.get(), 0);
  ASSERT_OK(status);
}

void WriteTestCase(const zx::channel& output) {
  uint32_t len =
      sizeof(fuchsia_driver_test_LoggerLogTestCaseRequest) + FIDL_ALIGN(strlen(kLogTestCaseName));
  FIDL_ALIGNDECL uint8_t bytes[len];
  fidl::Builder builder(bytes, len);

  auto* req = builder.New<fuchsia_driver_test_LoggerLogTestCaseRequest>();
  fidl_init_txn_header(&req->hdr, FIDL_TXID_NO_RESPONSE,
                       fuchsia_driver_test_LoggerLogTestCaseOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(strlen(kLogTestCaseName)));
  req->name.data = data;
  req->name.size = strlen(kLogTestCaseName);
  memcpy(data, kLogTestCaseName, strlen(kLogTestCaseName));

  req->result.passed = 1;
  req->result.failed = 2;
  req->result.skipped = 3;

  fidl::HLCPPOutgoingMessage msg(builder.Finalize(), fidl::HandleDispositionPart());
  const char* err = nullptr;
  zx_status_t status = msg.Encode(&fuchsia_driver_test_LoggerLogTestCaseRequestTable, &err);
  ASSERT_OK(status);
  status = msg.Write(output.get(), 0);
  ASSERT_OK(status);
}

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void CheckBindDriverReceived(
    const fidl::ServerEnd<fuchsia_device_manager::DeviceController>& controller,
    const char* expected_driver) {
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::IncomingMessage msg =
      fidl::ChannelReadEtc(controller.channel().get(), 0, fidl::BufferSpan(bytes, std::size(bytes)),
                           cpp20::span(handles));
  ASSERT_TRUE(msg.ok());

  auto* header = msg.header();
  FidlTransaction txn(header->txid, zx::unowned(controller.channel()));

  FakeDevice fake(zx::channel{}, expected_driver);
  ASSERT_EQ(fidl::WireDispatch(
                static_cast<fidl::WireServer<fuchsia_device_manager::DeviceController>*>(&fake),
                std::move(msg), &txn),
            fidl::DispatchResult::kFound);
  ASSERT_TRUE(fake.bind_called());
}

}  // namespace

class TestDriverTestReporter : public DriverTestReporter {
 public:
  explicit TestDriverTestReporter(const fbl::String& driver_name)
      : DriverTestReporter(driver_name) {}

  void LogMessage(const char* msg, size_t size) override {
    if (size != strlen(kLogMessage)) {
      return;
    }
    if (strncmp(msg, kLogMessage, size)) {
      return;
    }
    log_message_called = true;
  }

  void LogTestCase(const char* name, size_t name_size,
                   const fuchsia_driver_test_TestCaseResult* result) override {
    if (name_size != strlen(kLogTestCaseName)) {
      return;
    }
    if (strncmp(name, kLogTestCaseName, name_size)) {
      return;
    }
    if (result->passed != 1 || result->failed != 2 || result->skipped != 3) {
      return;
    }
    log_test_case_called = true;
  }

  void TestStart() override { start_called = true; }

  void TestFinished() override { finished_called = true; }

  bool log_message_called = false;
  bool log_test_case_called = false;
  bool start_called = false;
  bool finished_called = false;
};

TEST(MiscTestCase, InitCoreDevices) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
}

TEST(MiscTestCase, DumpState) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  constexpr int32_t kBufSize = 256;
  char buf[kBufSize + 1] = {0};

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kBufSize, 0, &vmo));
  VmoWriter writer(std::move(vmo));

  coordinator.DumpState(&writer);

  ASSERT_EQ(writer.written(), writer.available());
  ASSERT_LT(writer.written(), kBufSize);
  ASSERT_GT(writer.written(), 0);
  ASSERT_OK(writer.vmo().read(buf, 0, writer.written()));

  ASSERT_NE(nullptr, strstr(buf, "[root]"));
}

TEST(MiscTestCase, LoadDriver) {
  bool found_driver = false;
  auto callback = [&found_driver](Driver* drv, const char* version) {
    delete drv;
    found_driver = true;
  };
  load_driver(nullptr, kDriverPath, callback);
  ASSERT_TRUE(found_driver);
}

TEST(MiscTestCase, LoadDisabledDriver) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("mock-boot-args"));
  mock_boot_arguments::Server boot_args{{{"driver.mock_device.disable", "true"}}};
  fidl::WireSyncClient<fuchsia_boot::Arguments> client;
  boot_args.CreateClient(loop.dispatcher(), &client);

  bool found_driver = false;
  auto callback = [&found_driver](Driver* drv, const char* version) {
    delete drv;
    found_driver = true;
  };
  load_driver(&client, kDriverPath, callback);
  ASSERT_FALSE(found_driver);
}

TEST(MiscTestCase, BindDrivers) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
  coordinator.set_running(true);

  Driver* driver;
  auto callback = [&coordinator, &driver](Driver* drv, const char* version) {
    driver = drv;
    return coordinator.DriverAdded(drv, version);
  };
  load_driver(nullptr, kDriverPath, callback);
  loop.RunUntilIdle();
  ASSERT_EQ(1, coordinator.drivers().size_slow());
  ASSERT_EQ(driver, &coordinator.drivers().front());
}

// Test binding drivers against the root/test/misc devices
TEST(MiscTestCase, BindDriversForBuiltins) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  // AttemptBind function that asserts it has only been called once
  class CallOnce {
   public:
    explicit CallOnce(size_t line) : line_number_(line) {}
    CallOnce(const CallOnce&) = delete;
    CallOnce& operator=(const CallOnce&) = delete;

    CallOnce(CallOnce&& other) { *this = std::move(other); }
    CallOnce& operator=(CallOnce&& other) {
      if (this != &other) {
        line_number_ = other.line_number_;
        call_count_ = other.call_count_;
        // Ensure the dtor for the other one doesn't run
        other.call_count_ = 1;
      }
      return *this;
    }

    ~CallOnce() { EXPECT_EQ(1, call_count_, "Mismatch from line %zu\n", line_number_); }
    zx_status_t operator()(const Driver* drv, const fbl::RefPtr<Device>& dev) {
      ++call_count_;
      return ZX_OK;
    }

   private:
    size_t line_number_;
    size_t call_count_ = 0;
  };

  auto make_fake_driver = [](auto&& instructions) -> std::unique_ptr<Driver> {
    size_t instruction_count = std::size(instructions);
    auto binding = std::make_unique<zx_bind_inst_t[]>(instruction_count);
    memcpy(binding.get(), instructions, instruction_count * sizeof(instructions[0]));
    auto drv = std::make_unique<Driver>();
    drv->binding = std::move(binding);
    drv->bytecode_version = 1;
    drv->binding_size = static_cast<uint32_t>(instruction_count * sizeof(instructions[0]));
    return drv;
  };

  {
    zx_bind_inst_t test_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
    };
    auto test_drv = make_fake_driver(test_drv_bind);
    ASSERT_OK(coordinator.BindDriver(test_drv.get(), CallOnce{__LINE__}));
  }

  {
    zx_bind_inst_t misc_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
    };
    auto misc_drv = make_fake_driver(misc_drv_bind);
    ASSERT_OK(coordinator.BindDriver(misc_drv.get(), CallOnce{__LINE__}));
  }

  {
    zx_bind_inst_t root_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ROOT),
    };
    auto root_drv = make_fake_driver(root_drv_bind);
    ASSERT_OK(coordinator.BindDriver(root_drv.get(), CallOnce{__LINE__}));
  }

  {
    zx_bind_inst_t test_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    };
    auto test_drv = make_fake_driver(test_drv_bind);
    ASSERT_OK(coordinator.BindDriver(test_drv.get(), CallOnce{__LINE__}));
  }

  {
    zx_bind_inst_t misc_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    };
    auto misc_drv = make_fake_driver(misc_drv_bind);
    ASSERT_OK(coordinator.BindDriver(misc_drv.get(), CallOnce{__LINE__}));
  }

  {
    zx_bind_inst_t root_drv_bind[] = {
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_ROOT),
        BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    };
    auto root_drv = make_fake_driver(root_drv_bind);
    ASSERT_OK(coordinator.BindDriver(root_drv.get(), CallOnce{__LINE__}));
  }
}

TEST(MiscTestCase, BindDevices) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "mock-device", ZX_PROTOCOL_TEST,
      {} /* driver_path */, {} /* args */, false /* invisible */, false /* skip_autobind */,
      false /* has_init */, true /* always_init */, zx::vmo() /*inspect*/,
      zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  // Add the driver.
  load_driver(nullptr, kDriverPath, fit::bind_member(&coordinator, &Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // The device has no driver_host, so the init task should automatically complete.
  ASSERT_TRUE(device->is_visible());
  ASSERT_EQ(Device::State::kActive, device->state());

  // Bind the device to a fake driver_host.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.devices().front());
  auto host = fbl::MakeRefCounted<DriverHost>(
      &coordinator, fidl::ClientEnd<fuchsia_device_manager::DriverHostController>(),
      fidl::ClientEnd<fuchsia_io::Directory>(), zx::process{});
  dev->set_host(std::move(host));
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(controller_endpoints->server, kDriverPath));
  loop.RunUntilIdle();

  // Reset the fake driver_host connection.
  dev->set_host(nullptr);
  coordinator_endpoints->client.reset();
  controller_endpoints->server.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, TestOutput) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "mock-device", ZX_PROTOCOL_TEST,
      {} /* driver_path */, {} /* args */, false /* invisible */, false /* skip_autobind */,
      false /* has_init */, true /* always_init */, zx::vmo() /*inspect*/,
      zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  fbl::String driver_name;
  auto test_reporter_ = std::make_unique<TestDriverTestReporter>(driver_name);
  auto* test_reporter = test_reporter_.get();
  device->test_reporter = std::move(test_reporter_);

  // Add the driver.
  load_driver(nullptr, kDriverPath, fit::bind_member(&coordinator, &Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // The device has no driver_host, so the init task should automatically complete.
  ASSERT_TRUE(device->is_visible());
  ASSERT_EQ(Device::State::kActive, device->state());

  // Bind the device to a fake driver_host.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.devices().front());
  auto host = fbl::MakeRefCounted<DriverHost>(
      &coordinator, fidl::ClientEnd<fuchsia_device_manager::DriverHostController>(),
      fidl::ClientEnd<fuchsia_io::Directory>(), zx::process{});
  dev->set_host(std::move(host));
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  zx::channel test_device, test_coordinator;
  zx::channel::create(0, &test_device, &test_coordinator);
  ASSERT_NO_FATAL_FAILURES(
      BindDriverTestOutput(controller_endpoints->server, std::move(test_coordinator)));
  loop.RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(WriteTestLog(test_device));
  ASSERT_NO_FATAL_FAILURES(WriteTestCase(test_device));
  loop.RunUntilIdle();

  // The test logging handlers should not be called until the test is finished and the channel is
  // closed.
  EXPECT_FALSE(test_reporter->start_called);
  EXPECT_FALSE(test_reporter->log_message_called);
  EXPECT_FALSE(test_reporter->log_test_case_called);
  EXPECT_FALSE(test_reporter->finished_called);

  test_device.reset();
  loop.RunUntilIdle();
  EXPECT_TRUE(test_reporter->start_called);
  EXPECT_TRUE(test_reporter->log_message_called);
  EXPECT_TRUE(test_reporter->log_test_case_called);
  EXPECT_TRUE(test_reporter->finished_called);

  // Reset the fake driver_host connection.
  dev->set_host(nullptr);
  controller_endpoints->server.reset();
  coordinator_endpoints->client.reset();
  loop.RunUntilIdle();
}

void CompareStrProperty(const fuchsia_device_manager::wire::DeviceStrProperty expected,
                        const StrProperty actual) {
  ASSERT_STR_EQ(expected.key.get(), actual.key);

  if (expected.value.is_int_value()) {
    auto* value = std::get_if<uint32_t>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_EQ(expected.value.int_value(), *value);
  } else if (expected.value.is_str_value()) {
    auto* value = std::get_if<std::string>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_STR_EQ(expected.value.str_value(), *value);
  } else if (expected.value.is_bool_value()) {
    auto* value = std::get_if<bool>(&actual.value);
    ASSERT_TRUE(value);
    ASSERT_EQ(expected.value.bool_value(), *value);
  }
}

// Adds a device with the given properties to the device coordinator, then checks that the
// coordinator contains the device, and that its properties are correct.
void AddDeviceWithProperties(const fuchsia_device_manager::wire::DeviceProperty* props_data,
                             size_t props_count,
                             const fuchsia_device_manager::wire::DeviceStrProperty* str_props_data,
                             size_t str_props_count) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), props_data, props_count, str_props_data,
      str_props_count, "mock-device", ZX_PROTOCOL_TEST, {} /* driver_path */, {} /* args */,
      false /* invisible */, false /* skip_autobind */, false /* has_init */,
      true /* always_init */, zx::vmo() /*inspect*/, zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);

  // Check that the device has been added to the coordinator, with the correct properties.
  ASSERT_EQ(1, coordinator.devices().size_slow());
  const Device& dev = coordinator.devices().front();
  ASSERT_EQ(dev.props().size(), props_count);
  for (size_t i = 0; i < props_count; i++) {
    ASSERT_EQ(dev.props()[i].id, props_data[i].id);
    ASSERT_EQ(dev.props()[i].reserved, props_data[i].reserved);
    ASSERT_EQ(dev.props()[i].value, props_data[i].value);
  }

  ASSERT_EQ(dev.str_props().size(), str_props_count);
  for (size_t i = 0; i < str_props_count; i++) {
    CompareStrProperty(str_props_data[i], dev.str_props()[i]);
  }

  controller_endpoints->server.reset();
  coordinator_endpoints->client.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, DeviceProperties) {
  // No properties.
  AddDeviceWithProperties(nullptr, 0, nullptr, 0);

  // Multiple device properties. No string properties.
  fuchsia_device_manager::wire::DeviceProperty props[] = {
      fuchsia_device_manager::wire::DeviceProperty{1, 0, 1},
      fuchsia_device_manager::wire::DeviceProperty{2, 0, 1},
  };
  AddDeviceWithProperties(props, std::size(props), nullptr, 0);

  uint32_t int_val = 1000;
  auto str_val = fidl::StringView::FromExternal("timberdoodle");

  // Multiple device string properties. No device properties.
  fuchsia_device_manager::wire::DeviceStrProperty str_props[] = {
      fuchsia_device_manager::wire::DeviceStrProperty{
          "snipe", fuchsia_device_manager::wire::PropertyValue::WithStrValue(
                       fidl::ObjectView<fidl::StringView>::FromExternal(&str_val))},
      fuchsia_device_manager::wire::DeviceStrProperty{
          "sandpiper", fuchsia_device_manager::wire::PropertyValue::WithIntValue(
                           fidl::ObjectView<uint32_t>::FromExternal(&int_val))},
  };
  AddDeviceWithProperties(nullptr, 0, str_props, std::size(str_props));

  // Multiple device properties and device string properties.
  AddDeviceWithProperties(props, std::size(props), str_props, std::size(str_props));
}

TEST(MiscTestCase, InvalidStringProperties) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  InspectManager inspect_manager(loop.dispatcher());
  Coordinator coordinator(NullConfig(), &inspect_manager, loop.dispatcher());

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  // Create an invalid string with invalid UTF-8 characters.
  const char kInvalidStr[] = {static_cast<char>(0xC0), 0};
  auto str_val = fidl::StringView::FromExternal("ovenbird");
  fuchsia_device_manager::wire::DeviceStrProperty str_props[] = {
      fuchsia_device_manager::wire::DeviceStrProperty{
          fidl::StringView::FromExternal(kInvalidStr, std::size(kInvalidStr)),
          fuchsia_device_manager::wire::PropertyValue::WithStrValue(
              fidl::ObjectView<fidl::StringView>::FromExternal(&str_val))},
  };

  fbl::RefPtr<Device> device;
  auto status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props */, 0 /* props_count */, str_props,
      std::size(str_props), "mock-device", ZX_PROTOCOL_TEST, {} /* driver_path */, {} /* args */,
      false /* invisible */, false /* skip_autobind */, false /* has_init */,
      true /* always_init */, zx::vmo() /*inspect*/, zx::channel() /* client_remote */, &device);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
}

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
