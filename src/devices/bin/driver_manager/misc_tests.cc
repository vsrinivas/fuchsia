// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/driver/test/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>

#include <vector>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/algorithm.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "coordinator_test_utils.h"
#include "devfs.h"
#include "devhost.h"
#include "driver_test_reporter.h"
#include "fdio.h"

constexpr char kDriverPath[] = "/boot/driver/test/mock-device.so";
constexpr char kLogMessage[] = "log message text";
constexpr char kLogTestCaseName[] = "log test case";

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void BindDriverTestOutput(const zx::channel& remote, zx::channel test_output) {
  // Read the BindDriver request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(1, actual_handles);
  status = zx_handle_close(handles[0]);
  ASSERT_OK(status);

  // Validate the BindDriver request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerBindDriverGenOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerBindDriverRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  zx_txid_t txid = hdr->txid;
  ASSERT_OK(status);

  // Write the BindDriver response.
  memset(bytes, 0, sizeof(bytes));
  auto resp = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverResponse*>(bytes);
  fidl_init_txn_header(&resp->hdr, txid,
                       fuchsia_device_manager_DeviceControllerBindDriverGenOrdinal);
  resp->status = ZX_OK;
  resp->test_output = test_output.release();
  status = fidl_encode(&fuchsia_device_manager_DeviceControllerBindDriverResponseTable, bytes,
                       sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(1, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), handles, actual_handles);
  ASSERT_OK(status);
}

void WriteTestLog(const zx::channel& output) {
  uint32_t len =
      sizeof(fuchsia_driver_test_LoggerLogMessageRequest) + FIDL_ALIGN(strlen(kLogMessage));
  FIDL_ALIGNDECL uint8_t bytes[len];
  fidl::Builder builder(bytes, len);

  auto* req = builder.New<fuchsia_driver_test_LoggerLogMessageRequest>();
  fidl_init_txn_header(&req->hdr, FIDL_TXID_NO_RESPONSE,
                       fuchsia_driver_test_LoggerLogMessageGenOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(strlen(kLogMessage)));
  req->msg.data = data;
  req->msg.size = strlen(kLogMessage);
  memcpy(data, kLogMessage, strlen(kLogMessage));

  fidl::Message msg(builder.Finalize(), fidl::HandlePart());
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
                       fuchsia_driver_test_LoggerLogTestCaseGenOrdinal);

  auto* data = builder.NewArray<char>(static_cast<uint32_t>(strlen(kLogTestCaseName)));
  req->name.data = data;
  req->name.size = strlen(kLogTestCaseName);
  memcpy(data, kLogTestCaseName, strlen(kLogTestCaseName));

  req->result.passed = 1;
  req->result.failed = 2;
  req->result.skipped = 3;

  fidl::Message msg(builder.Finalize(), fidl::HandlePart());
  const char* err = nullptr;
  zx_status_t status = msg.Encode(&fuchsia_driver_test_LoggerLogTestCaseRequestTable, &err);
  ASSERT_OK(status);
  status = msg.Write(output.get(), 0);
  ASSERT_OK(status);
}

// Reads a BindDriver request from remote, checks that it is for the expected
// driver, and then sends a ZX_OK response.
void CheckBindDriverReceived(const zx::channel& remote, const char* expected_driver) {
  // Read the BindDriver request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(1, actual_handles);
  status = zx_handle_close(handles[0]);
  ASSERT_OK(status);

  // Validate the BindDriver request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerBindDriverGenOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerBindDriverRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  zx_txid_t txid = hdr->txid;
  ASSERT_OK(status);
  auto req = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverRequest*>(bytes);
  ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                  reinterpret_cast<const uint8_t*>(req->driver_path.data), req->driver_path.size,
                  "");

  // Write the BindDriver response.
  memset(bytes, 0, sizeof(bytes));
  auto resp = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverResponse*>(bytes);
  fidl_init_txn_header(&resp->hdr, txid,
                       fuchsia_device_manager_DeviceControllerBindDriverGenOrdinal);
  resp->status = ZX_OK;
  status = fidl_encode(&fuchsia_device_manager_DeviceControllerBindDriverResponseTable, bytes,
                       sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
  ASSERT_OK(status);
}

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
  Coordinator coordinator(DefaultConfig(nullptr, nullptr, nullptr, nullptr));

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
}

TEST(MiscTestCase, DumpState) {
  Coordinator coordinator(DefaultConfig(nullptr, nullptr, nullptr, nullptr));

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
  load_driver(kDriverPath, callback);
  ASSERT_TRUE(found_driver);
}

TEST(MiscTestCase, BindDrivers) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr, nullptr, nullptr));

  zx_status_t status = coordinator.InitCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
  coordinator.set_running(true);

  Driver* driver;
  auto callback = [&coordinator, &driver](Driver* drv, const char* version) {
    driver = drv;
    return coordinator.DriverAdded(drv, version);
  };
  load_driver(kDriverPath, callback);
  loop.RunUntilIdle();
  ASSERT_EQ(1, coordinator.drivers().size_slow());
  ASSERT_EQ(driver, &coordinator.drivers().front());
}

// Test binding drivers against the root/test/misc devices
TEST(MiscTestCase, BindDriversForBuiltins) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr, nullptr, nullptr));

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
    size_t instruction_count = fbl::count_of(instructions);
    auto binding = std::make_unique<zx_bind_inst_t[]>(instruction_count);
    memcpy(binding.get(), instructions, instruction_count * sizeof(instructions[0]));
    auto drv = std::make_unique<Driver>();
    drv->binding.reset(binding.release());
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
  Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr, nullptr, nullptr));

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  zx::channel coordinator_local, coordinator_remote;
  zx_status_t status = zx::channel::create(0, &coordinator_local, &coordinator_remote);
  ASSERT_OK(status);

  zx::channel controller_local, controller_remote;
  status = zx::channel::create(0, &controller_local, &controller_remote);
  ASSERT_OK(status);

  fbl::RefPtr<Device> device;
  status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_local), std::move(coordinator_local),
      nullptr /* props_data */, 0 /* props_count */, "mock-device", ZX_PROTOCOL_TEST,
      nullptr /* driver_path */, nullptr /* args */, false /* invisible */, false /* has_init */,
      true /* always_init */, zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  // Add the driver.
  load_driver(kDriverPath, fit::bind_member(&coordinator, &Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // The device has no devhost, so the init task should automatically complete.
  ASSERT_TRUE(device->is_visible());
  ASSERT_EQ(Device::State::kActive, device->state());

  // Bind the device to a fake devhost.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.devices().front());
  auto host = fbl::MakeRefCounted<Devhost>(&coordinator, zx::channel{});
  dev->set_host(std::move(host));
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(controller_remote, kDriverPath));
  loop.RunUntilIdle();

  // Reset the fake devhost connection.
  dev->set_host(nullptr);
  coordinator_remote.reset();
  controller_remote.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, TestOutput) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr, nullptr, nullptr));

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  zx::channel coordinator_local, coordinator_remote;
  zx_status_t status = zx::channel::create(0, &coordinator_local, &coordinator_remote);
  ASSERT_OK(status);

  zx::channel controller_local, controller_remote;
  status = zx::channel::create(0, &controller_local, &controller_remote);
  ASSERT_OK(status);

  fbl::RefPtr<Device> device;
  status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_local), std::move(coordinator_local),
      nullptr /* props_data */, 0 /* props_count */, "mock-device", ZX_PROTOCOL_TEST,
      nullptr /* driver_path */, nullptr /* args */, false /* invisible */, false /* has_init */,
      true /* always_init */, zx::channel() /* client_remote */, &device);
  device->AddRef();  // refcount starts at zero, so bump it up to keep us from being cleaned up
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  fbl::String driver_name;
  auto test_reporter_ = std::make_unique<TestDriverTestReporter>(driver_name);
  auto* test_reporter = test_reporter_.get();
  device->test_reporter = std::move(test_reporter_);

  // Add the driver.
  load_driver(kDriverPath, fit::bind_member(&coordinator, &Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // The device has no devhost, so the init task should automatically complete.
  ASSERT_TRUE(device->is_visible());
  ASSERT_EQ(Device::State::kActive, device->state());

  // Bind the device to a fake devhost.
  fbl::RefPtr<Device> dev = fbl::RefPtr(&coordinator.devices().front());
  auto host = fbl::MakeRefCounted<Devhost>(&coordinator, zx::channel{});
  dev->set_host(std::move(host));
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  zx::channel test_device, test_coordinator;
  zx::channel::create(0, &test_device, &test_coordinator);
  ASSERT_NO_FATAL_FAILURES(BindDriverTestOutput(controller_remote, std::move(test_coordinator)));
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

  // Reset the fake devhost connection.
  dev->set_host(nullptr);
  controller_remote.reset();
  coordinator_remote.reset();
  loop.RunUntilIdle();
}

// Adds a device with the given properties to the device coordinator, then checks that the
// coordinator contains the device, and that its properties are correct.
void AddDeviceWithProperties(const llcpp::fuchsia::device::manager::DeviceProperty* props_data,
                             size_t props_count) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr, nullptr, nullptr));

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  zx::channel coordinator_local, coordinator_remote;
  zx_status_t status = zx::channel::create(0, &coordinator_local, &coordinator_remote);
  ASSERT_OK(status);

  zx::channel controller_local, controller_remote;
  status = zx::channel::create(0, &controller_local, &controller_remote);
  ASSERT_OK(status);

  fbl::RefPtr<Device> device;
  status = coordinator.AddDevice(
      coordinator.test_device(), std::move(controller_local), std::move(coordinator_local),
      props_data, props_count, "mock-device", ZX_PROTOCOL_TEST, nullptr /* driver_path */,
      nullptr /* args */, false /* invisible */, false /* has_init */, true /* always_init */,
      zx::channel() /* client_remote */, &device);
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

  controller_remote.reset();
  coordinator_remote.reset();
  loop.RunUntilIdle();
}

TEST(MiscTestCase, DeviceProperties) {
  // No properties.
  AddDeviceWithProperties(nullptr, 0);

  // Multiple properties.
  llcpp::fuchsia::device::manager::DeviceProperty props[] = {
      llcpp::fuchsia::device::manager::DeviceProperty{1, 0, 1},
      llcpp::fuchsia::device::manager::DeviceProperty{2, 0, 1},
  };
  AddDeviceWithProperties(props, std::size(props));
}

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
