// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coordinator.h"

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

#include "devfs.h"
#include "devhost.h"
#include "driver-test-reporter.h"
#include "fdio.h"

namespace {

constexpr char kSystemDriverPath[] = "/boot/driver/platform-bus.so";
constexpr char kDriverPath[] = "/boot/driver/test/mock-device.so";

constexpr char kLogMessage[] = "log message text";
constexpr char kLogTestCaseName[] = "log test case";

class DummyFsProvider : public devmgr::FsProvider {
  ~DummyFsProvider() {}
  zx::channel CloneFs(const char* path) override { return zx::channel(); }
};

void CreateBootArgs(const char* config, size_t size, devmgr::BootArgs* boot_args) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  ASSERT_OK(status);

  status = vmo.write(config, 0, size);
  ASSERT_OK(status);

  status = devmgr::BootArgs::Create(std::move(vmo), size, boot_args);
  ASSERT_OK(status);
}

devmgr::CoordinatorConfig DefaultConfig(async_dispatcher_t* dispatcher,
                                        devmgr::BootArgs* boot_args) {
  devmgr::CoordinatorConfig config{};
  const char config1[] = "key1=old-value\0key2=value2\0key1=new-value";
  if (boot_args != nullptr) {
    CreateBootArgs(config1, sizeof(config1), boot_args);
  }
  config.dispatcher = dispatcher;
  config.require_system = false;
  config.asan_drivers = false;
  config.boot_args = boot_args;
  config.fs_provider = new DummyFsProvider();
  config.suspend_fallback = true;
  zx::event::create(0, &config.fshost_event);
  return config;
}

TEST(CoordinatorTestCase, InitializeCoreDevices) {
  devmgr::Coordinator coordinator(DefaultConfig(nullptr, nullptr));

  zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
}

TEST(CoordinatorTestCase, DumpState) {
  devmgr::Coordinator coordinator(DefaultConfig(nullptr, nullptr));

  zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  constexpr int32_t kBufSize = 256;
  char buf[kBufSize + 1] = {0};

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(kBufSize, 0, &vmo));
  devmgr::VmoWriter writer(std::move(vmo));

  coordinator.DumpState(&writer);

  ASSERT_EQ(writer.written(), writer.available());
  ASSERT_LT(writer.written(), kBufSize);
  ASSERT_GT(writer.written(), 0);
  ASSERT_OK(writer.vmo().read(buf, 0, writer.written()));

  ASSERT_NE(nullptr, strstr(buf, "[root]"));
}

TEST(CoordinatorTestCase, LoadDriver) {
  bool found_driver = false;
  auto callback = [&found_driver](devmgr::Driver* drv, const char* version) {
    delete drv;
    found_driver = true;
  };
  devmgr::load_driver(kDriverPath, callback);
  ASSERT_TRUE(found_driver);
}

TEST(CoordinatorTestCase, BindDrivers) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr));

  zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);
  coordinator.set_running(true);

  devmgr::Driver* driver;
  auto callback = [&coordinator, &driver](devmgr::Driver* drv, const char* version) {
    driver = drv;
    return coordinator.DriverAdded(drv, version);
  };
  devmgr::load_driver(kDriverPath, callback);
  loop.RunUntilIdle();
  ASSERT_EQ(1, coordinator.drivers().size_slow());
  ASSERT_EQ(driver, &coordinator.drivers().front());
}

// Test binding drivers against the root/test/misc devices
TEST(CoordinatorTestCase, BindDriversForBuiltins) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr));

  zx_status_t status = coordinator.InitializeCoreDevices(kSystemDriverPath);
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
    zx_status_t operator()(const devmgr::Driver* drv, const fbl::RefPtr<devmgr::Device>& dev) {
      ++call_count_;
      return ZX_OK;
    }

   private:
    size_t line_number_;
    size_t call_count_ = 0;
  };

  auto make_fake_driver = [](auto&& instructions) -> std::unique_ptr<devmgr::Driver> {
    size_t instruction_count = fbl::count_of(instructions);
    auto binding = std::make_unique<zx_bind_inst_t[]>(instruction_count);
    memcpy(binding.get(), instructions, instruction_count * sizeof(instructions[0]));
    auto drv = std::make_unique<devmgr::Driver>();
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

void InitializeCoordinator(devmgr::Coordinator* coordinator) {
  zx_status_t status = coordinator->InitializeCoreDevices(kSystemDriverPath);
  ASSERT_OK(status);

  // Load the component driver
  devmgr::load_driver(devmgr::kComponentDriverPath,
                      fit::bind_member(coordinator, &devmgr::Coordinator::DriverAddedInit));

  // Add the driver we're using as platform bus
  devmgr::load_driver(kSystemDriverPath,
                      fit::bind_member(coordinator, &devmgr::Coordinator::DriverAddedInit));

  // Initialize devfs.
  devmgr::devfs_init(coordinator->root_device(), coordinator->dispatcher());
  status = devmgr::devfs_publish(coordinator->root_device(), coordinator->test_device());
  status = devmgr::devfs_publish(coordinator->root_device(), coordinator->sys_device());
  ASSERT_OK(status);
  coordinator->set_running(true);
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
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerBindDriverOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerBindDriverRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
  auto req = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverRequest*>(bytes);
  ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                  reinterpret_cast<const uint8_t*>(req->driver_path.data), req->driver_path.size,
                  "");

  // Write the BindDriver response.
  memset(bytes, 0, sizeof(bytes));
  auto resp = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverResponse*>(bytes);
  resp->hdr.ordinal = fuchsia_device_manager_DeviceControllerBindDriverOrdinal;
  resp->status = ZX_OK;
  status = fidl_encode(&fuchsia_device_manager_DeviceControllerBindDriverResponseTable, bytes,
                       sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
  ASSERT_OK(status);
}

TEST(CoordinatorTestCase, BindDevices) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr));

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_OK(status);
  fbl::RefPtr<devmgr::Device> device;
  status = coordinator.AddDevice(coordinator.test_device(), std::move(local),
                                 nullptr /* props_data */, 0 /* props_count */, "mock-device",
                                 ZX_PROTOCOL_TEST, nullptr /* driver_path */, nullptr /* args */,
                                 false /* invisible */, zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  // Add the driver.
  devmgr::load_driver(kDriverPath,
                      fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // Bind the device to a fake devhost.
  fbl::RefPtr<devmgr::Device> dev = fbl::RefPtr(&coordinator.devices().front());
  devmgr::Devhost host;
  host.AddRef();  // refcount starts at zero, so bump it up to keep us from being cleaned up
  dev->set_host(&host);
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(remote, kDriverPath));
  loop.RunUntilIdle();

  // Reset the fake devhost connection.
  dev->set_host(nullptr);
  remote.reset();
  loop.RunUntilIdle();
}

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
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerBindDriverOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerBindDriverRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);

  // Write the BindDriver response.
  memset(bytes, 0, sizeof(bytes));
  auto resp = reinterpret_cast<fuchsia_device_manager_DeviceControllerBindDriverResponse*>(bytes);
  resp->hdr.ordinal = fuchsia_device_manager_DeviceControllerBindDriverOrdinal;
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
  req->hdr.ordinal = fuchsia_driver_test_LoggerLogMessageOrdinal;
  req->hdr.txid = FIDL_TXID_NO_RESPONSE;

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
  req->hdr.ordinal = fuchsia_driver_test_LoggerLogTestCaseOrdinal;
  req->hdr.txid = FIDL_TXID_NO_RESPONSE;

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

class TestDriverTestReporter : public devmgr::DriverTestReporter {
 public:
  explicit TestDriverTestReporter(const fbl::String& driver_name)
      : devmgr::DriverTestReporter(driver_name) {}

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

TEST(CoordinatorTestCase, TestOutput) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr::Coordinator coordinator(DefaultConfig(loop.dispatcher(), nullptr));

  ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator));

  // Add the device.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_OK(status);
  fbl::RefPtr<devmgr::Device> device;
  status = coordinator.AddDevice(coordinator.test_device(), std::move(local),
                                 nullptr /* props_data */, 0 /* props_count */, "mock-device",
                                 ZX_PROTOCOL_TEST, nullptr /* driver_path */, nullptr /* args */,
                                 false /* invisible */, zx::channel() /* client_remote */, &device);
  ASSERT_OK(status);
  ASSERT_EQ(1, coordinator.devices().size_slow());

  fbl::String driver_name;
  auto test_reporter_ = std::make_unique<TestDriverTestReporter>(driver_name);
  auto* test_reporter = test_reporter_.get();
  device->test_reporter = std::move(test_reporter_);

  // Add the driver.
  devmgr::load_driver(kDriverPath,
                      fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAdded));
  loop.RunUntilIdle();
  ASSERT_FALSE(coordinator.drivers().is_empty());

  // Bind the device to a fake devhost.
  fbl::RefPtr<devmgr::Device> dev = fbl::RefPtr(&coordinator.devices().front());
  devmgr::Devhost host;
  host.AddRef();  // refcount starts at zero, so bump it up to keep us from being cleaned up
  dev->set_host(&host);
  status = coordinator.BindDevice(dev, kDriverPath, true /* new device */);
  ASSERT_OK(status);

  // Check the BindDriver request.
  zx::channel test_device, test_coordinator;
  zx::channel::create(0, &test_device, &test_coordinator);
  ASSERT_NO_FATAL_FAILURES(BindDriverTestOutput(remote, std::move(test_coordinator)));
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
  remote.reset();
  loop.RunUntilIdle();
}

// Reads a CreateDevice from remote, checks expectations, and sends a ZX_OK
// response.
void CheckCreateDeviceReceived(const zx::channel& remote, const char* expected_driver,
                               zx::channel* device_remote) {
  // Read the CreateDevice request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(3, actual_handles);
  *device_remote = zx::channel(handles[0]);
  status = zx_handle_close(handles[1]);
  ASSERT_OK(status);

  // Validate the CreateDevice request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DevhostControllerCreateDeviceOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DevhostControllerCreateDeviceRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
  auto req = reinterpret_cast<fuchsia_device_manager_DevhostControllerCreateDeviceRequest*>(bytes);
  ASSERT_EQ(req->driver_path.size, strlen(expected_driver));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_driver),
                  reinterpret_cast<const uint8_t*>(req->driver_path.data), req->driver_path.size,
                  "");
}

// Reads a Suspend request from remote and checks that it is for the expected
// flags, without sending a response. |SendSuspendReply| can be used to send the desired response.
void CheckSuspendReceived(const zx::channel& remote, uint32_t expected_flags) {
  // Read the Suspend request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  // Validate the Suspend request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerSuspendOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerSuspendRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
  auto req = reinterpret_cast<fuchsia_device_manager_DeviceControllerSuspendRequest*>(bytes);
  ASSERT_EQ(req->flags, expected_flags);
}

// Sends a response with the given return_status. This can be used to reply to a
// request received by |CheckSuspendReceived|.
void SendSuspendReply(const zx::channel& remote, zx_status_t return_status) {
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_handles;

  // Write the Suspend response.
  memset(bytes, 0, sizeof(bytes));
  auto resp = reinterpret_cast<fuchsia_device_manager_DeviceControllerSuspendResponse*>(bytes);
  resp->hdr.ordinal = fuchsia_device_manager_DeviceControllerSuspendOrdinal;
  resp->status = return_status;
  zx_status_t status =
      fidl_encode(&fuchsia_device_manager_DeviceControllerSuspendResponseTable, bytes,
                  sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
  ASSERT_OK(status);
}

// Reads a Suspend request from remote, checks that it is for the expected
// flags, and then sends the given response.
void CheckSuspendReceived(const zx::channel& remote, uint32_t expected_flags,
                          zx_status_t return_status) {
  CheckSuspendReceived(remote, expected_flags);
  SendSuspendReply(remote, return_status);
}

// Reads a CreateCompositeDevice from remote, checks expectations, and sends
// a ZX_OK response.
void CheckCreateCompositeDeviceReceived(const zx::channel& remote, const char* expected_name,
                                        size_t expected_components_count,
                                        zx::channel* composite_remote) {
  // Read the CreateCompositeDevice request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(1, actual_handles);
  composite_remote->reset(handles[0]);

  // Validate the CreateCompositeDevice request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DevhostControllerCreateCompositeDeviceOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequestTable,
                       bytes, actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
  auto req =
      reinterpret_cast<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest*>(
          bytes);
  ASSERT_EQ(req->name.size, strlen(expected_name));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_name),
                  reinterpret_cast<const uint8_t*>(req->name.data), req->name.size, "");
  ASSERT_EQ(expected_components_count, req->components.count);

  // Write the CreateCompositeDevice response.
  memset(bytes, 0, sizeof(bytes));
  auto resp =
      reinterpret_cast<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceResponse*>(
          bytes);
  resp->hdr.ordinal = fuchsia_device_manager_DevhostControllerCreateCompositeDeviceOrdinal;
  resp->status = ZX_OK;
  status =
      fidl_encode(&fuchsia_device_manager_DevhostControllerCreateCompositeDeviceResponseTable,
                  bytes, sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
  ASSERT_OK(status);
}

// Helper for BindComposite for issuing an AddComposite for a composite with the
// given components.  It's assumed that these components are children of
// the platform_bus and have the given protocol_id
void BindCompositeDefineComposite(const fbl::RefPtr<devmgr::Device>& platform_bus,
                                  const uint32_t* protocol_ids, size_t component_count,
                                  const zx_device_prop_t* props, size_t props_count,
                                  const char* name, zx_status_t expected_status = ZX_OK) {
  std::vector<llcpp::fuchsia::device::manager::DeviceComponent> components = {};
  for (size_t i = 0; i < component_count; ++i) {
    // Define a union type to avoid violating the strict aliasing rule.

    zx_bind_inst_t always = BI_MATCH();
    zx_bind_inst_t protocol = BI_MATCH_IF(EQ, BIND_PROTOCOL, protocol_ids[i]);

    llcpp::fuchsia::device::manager::DeviceComponent component;  // = &components[i];
    component.parts_count = 2;
    component.parts[0].match_program_count = 1;
    component.parts[0].match_program[0] = ::llcpp::fuchsia::device::manager::BindInstruction{
        .op = always.op,
        .arg = always.arg,
    };
    component.parts[1].match_program_count = 1;
    component.parts[1].match_program[0] = ::llcpp::fuchsia::device::manager::BindInstruction{
        .op = protocol.op,
        .arg = protocol.arg,
    };
    components.push_back(component);
  }

  auto prop_view = ::fidl::VectorView<uint64_t>(
      reinterpret_cast<uint64_t*>(const_cast<zx_device_prop_t*>(props)), props_count);
  devmgr::Coordinator* coordinator = platform_bus->coordinator;
  ASSERT_EQ(
      coordinator->AddCompositeDevice(platform_bus, name, prop_view, ::fidl::VectorView(components),
                                      0 /* coresident index */),
      expected_status);
}

struct DeviceState {
  // The representation in the coordinator of the device
  fbl::RefPtr<devmgr::Device> device;
  // The remote end of the channel that the coordinator is talking to
  zx::channel remote;
};

class MultipleDeviceTestCase : public zxtest::Test {
 public:
  ~MultipleDeviceTestCase() override = default;

  async::Loop* coordinator_loop() { return &coordinator_loop_; }
  bool coordinator_loop_thread_running() { return coordinator_loop_thread_running_; }
  void set_coordinator_loop_thread_running(bool value) {
    coordinator_loop_thread_running_ = value;
  }
  devmgr::Coordinator* coordinator() { return &coordinator_; }

  devmgr::Devhost* devhost() { return &devhost_; }
  const zx::channel& devhost_remote() { return devhost_remote_; }

  const fbl::RefPtr<devmgr::Device>& platform_bus() const { return platform_bus_.device; }
  const zx::channel& platform_bus_remote() const { return platform_bus_.remote; }
  DeviceState* device(size_t index) const { return &devices_[index]; }

  void AddDevice(const fbl::RefPtr<devmgr::Device>& parent, const char* name, uint32_t protocol_id,
                 fbl::String driver, size_t* device_index);
  void RemoveDevice(size_t device_index);

  bool DeviceHasPendingMessages(size_t device_index);
  bool DeviceHasPendingMessages(const zx::channel& remote);

  void DoSuspend(uint32_t flags);
  void DoSuspend(uint32_t flags, fit::function<void(uint32_t)> suspend_cb);

  void CheckUnbindReceived(const zx::channel& remote);
  void SendUnbindReply(const zx::channel& remote);
  void CheckUnbindReceivedAndReply(const zx::channel& remote);
  void CheckRemoveReceived(const zx::channel& remote);
  void SendRemoveReply(const zx::channel& remote);
  void CheckRemoveReceivedAndReply(const zx::channel& remote);

 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURES(InitializeCoordinator(&coordinator_));

    // refcount starts at zero, so bump it up to keep us from being cleaned up
    devhost_.AddRef();
    {
      zx::channel local;
      zx_status_t status = zx::channel::create(0, &local, &devhost_remote_);
      ASSERT_OK(status);
      devhost_.set_hrpc(local.release());
    }

    // Set up the sys device proxy, inside of the devhost
    ASSERT_OK(coordinator_.PrepareProxy(coordinator_.sys_device(), &devhost_));
    coordinator_loop_.RunUntilIdle();
    ASSERT_NO_FATAL_FAILURES(
        CheckCreateDeviceReceived(devhost_remote_, kSystemDriverPath, &sys_proxy_remote_));
    coordinator_loop_.RunUntilIdle();

    // Create a child of the sys_device (an equivalent of the platform bus)
    {
      zx::channel local;
      zx_status_t status = zx::channel::create(0, &local, &platform_bus_.remote);
      ASSERT_OK(status);
      status = coordinator_.AddDevice(
          coordinator_.sys_device()->proxy(), std::move(local), nullptr /* props_data */,
          0 /* props_count */, "platform-bus", 0, nullptr /* driver_path */, nullptr /* args */,
          false /* invisible */, zx::channel() /* client_remote */, &platform_bus_.device);
      ASSERT_OK(status);
      coordinator_loop_.RunUntilIdle();
    }
  }

  void TearDown() override {
    if (!coordinator_loop_thread_running_) {
      coordinator_loop_.RunUntilIdle();
    }
    // Remove the devices in the opposite order that we added them
    while (!devices_.is_empty()) {
      devices_.pop_back();
      if (!coordinator_loop_thread_running_) {
        coordinator_loop_.RunUntilIdle();
      }
    }
    platform_bus_.device.reset();
    if (!coordinator_loop_thread_running_) {
      coordinator_loop_.RunUntilIdle();
    }

    devhost_.devices().clear();
  }

  // The fake devhost that the platform bus is put into
  devmgr::Devhost devhost_;

  // The remote end of the channel that the coordinator uses to talk to the
  // devhost
  zx::channel devhost_remote_;

  // The remote end of the channel that the coordinator uses to talk to the
  // sys device proxy
  zx::channel sys_proxy_remote_;

  // The device object representing the platform bus driver (child of the
  // sys proxy)
  DeviceState platform_bus_;

  // These should be listed after devhost/sys_proxy as it needs to be
  // destroyed before them.
  async::Loop coordinator_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  bool coordinator_loop_thread_running_ = false;
  devmgr::BootArgs boot_args_;
  devmgr::Coordinator coordinator_{DefaultConfig(coordinator_loop_.dispatcher(), &boot_args_)};

  // A list of all devices that were added during this test, and their
  // channels.  These exist to keep them alive until the test is over.
  fbl::Vector<DeviceState> devices_;
};

void MultipleDeviceTestCase::AddDevice(const fbl::RefPtr<devmgr::Device>& parent, const char* name,
                                       uint32_t protocol_id, fbl::String driver, size_t* index) {
  DeviceState state;

  zx::channel local;
  zx_status_t status = zx::channel::create(0, &local, &state.remote);
  ASSERT_OK(status);
  status = coordinator_.AddDevice(
      parent, std::move(local), nullptr /* props_data */, 0 /* props_count */, name, protocol_id,
      driver.data() /* driver_path */, nullptr /* args */, false /* invisible */,
      zx::channel() /* client_remote */, &state.device);
  state.device->flags |= DEV_CTX_ALLOW_MULTI_COMPOSITE;
  ASSERT_OK(status);
  coordinator_loop_.RunUntilIdle();

  devices_.push_back(std::move(state));
  *index = devices_.size() - 1;
}

void MultipleDeviceTestCase::RemoveDevice(size_t device_index) {
  auto& state = devices_[device_index];
  ASSERT_OK(coordinator_.RemoveDevice(state.device, false));
  state.device.reset();
  state.remote.reset();
  coordinator_loop_.RunUntilIdle();
}

bool MultipleDeviceTestCase::DeviceHasPendingMessages(const zx::channel& remote) {
  return remote.wait_one(ZX_CHANNEL_READABLE, zx::time(0), nullptr) == ZX_OK;
}
bool MultipleDeviceTestCase::DeviceHasPendingMessages(size_t device_index) {
  return DeviceHasPendingMessages(devices_[device_index].remote);
}

void MultipleDeviceTestCase::DoSuspend(uint32_t flags,
                                       fit::function<void(uint32_t flags)> suspend_cb) {
  const bool vfs_exit_expected = (flags != DEVICE_SUSPEND_FLAG_SUSPEND_RAM);
  if (vfs_exit_expected) {
    zx::unowned_event event(coordinator()->fshost_event());
    auto thrd_func = [](void* ctx) -> int {
      zx::unowned_event event(*static_cast<zx::unowned_event*>(ctx));
      if (event->wait_one(FSHOST_SIGNAL_EXIT, zx::time::infinite(), nullptr) != ZX_OK) {
        return false;
      }
      if (event->signal(0, FSHOST_SIGNAL_EXIT_DONE) != ZX_OK) {
        return false;
      }
      return true;
    };

    thrd_t fshost_thrd;
    ASSERT_EQ(thrd_create(&fshost_thrd, thrd_func, &event), thrd_success);

    suspend_cb(flags);
    if (!coordinator_loop_thread_running()) {
      coordinator_loop()->RunUntilIdle();
    }
    int thread_status;
    ASSERT_EQ(thrd_join(fshost_thrd, &thread_status), thrd_success);
    ASSERT_TRUE(thread_status);

    // Make sure that vfs_exit() happened.
    ASSERT_OK(
        coordinator()->fshost_event().wait_one(FSHOST_SIGNAL_EXIT_DONE, zx::time(0), nullptr));
  } else {
    suspend_cb(flags);
    if (!coordinator_loop_thread_running()) {
      coordinator_loop()->RunUntilIdle();
    }

    // Make sure that vfs_exit() didn't happen.
    ASSERT_EQ(coordinator()->fshost_event().wait_one(FSHOST_SIGNAL_EXIT | FSHOST_SIGNAL_EXIT_DONE,
                                                     zx::time(0), nullptr),
              ZX_ERR_TIMED_OUT);
  }
}

void MultipleDeviceTestCase::DoSuspend(uint32_t flags) {
  DoSuspend(flags, [this](uint32_t flags) { coordinator()->Suspend(flags); });
}

TEST_F(MultipleDeviceTestCase, RemoveDeadDevice) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "device", 0 /* protocol id */, "", &index));

  auto& state = devices_[index];
  ASSERT_OK(coordinator_.RemoveDevice(state.device, false));

  ASSERT_FALSE(state.device->is_bindable());

  ASSERT_NOT_OK(coordinator_.RemoveDevice(state.device, false), "device should already be dead");
}

// Reads the request from |remote| and verifies whether it matches the expected Unbind request.
// |SendUnbindReply| can be used to send the desired response.
void MultipleDeviceTestCase::CheckUnbindReceived(const zx::channel& remote) {
  // Read the unbind request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  // Validate the unbind request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerUnbindOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerUnbindRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
}

// Sends a response with the given return_status. This can be used to reply to a
// request received by |CheckUnbindReceived|.
void MultipleDeviceTestCase::SendUnbindReply(const zx::channel& remote) {
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_handles;
  // Write the UnbindDone message.
  memset(bytes, 0, sizeof(bytes));
  auto req = reinterpret_cast<fuchsia_device_manager_CoordinatorUnbindDoneRequest*>(bytes);
  req->hdr.ordinal = fuchsia_device_manager_CoordinatorUnbindDoneOrdinal;
  req->hdr.txid = 1;
  zx_status_t status =
      fidl_encode(&fuchsia_device_manager_CoordinatorUnbindDoneRequestTable, bytes, sizeof(*req),
                  handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*req), nullptr, 0);
  ASSERT_OK(status);

  coordinator_loop()->RunUntilIdle();

  // Verify the UnbindDone response.
  uint32_t actual_bytes;
  status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                       &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  fidl::EncodedMessage<::llcpp::fuchsia::device::manager::Coordinator::UnbindDoneResponse> encoded(
      fidl::BytePart(bytes, actual_bytes, actual_bytes));
  auto decode_result = fidl::Decode(std::move(encoded));
  ASSERT_OK(decode_result.status);

  const ::llcpp::fuchsia::device::manager::Coordinator::UnbindDoneResponse& msg =
      *decode_result.message.message();
  ASSERT_FALSE(msg.result.is_err());
}

void MultipleDeviceTestCase::CheckUnbindReceivedAndReply(const zx::channel& remote) {
  CheckUnbindReceived(remote);
  SendUnbindReply(remote);
}

// Reads the request from |remote| and verifies whether it matches the expected
// CompleteRemoval request.
// |SendRemoveReply| can be used to send the desired response.
void MultipleDeviceTestCase::CheckRemoveReceived(const zx::channel& remote) {
  // Read the remove request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  // Validate the remove request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DeviceControllerCompleteRemovalOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DeviceControllerCompleteRemovalRequestTable, bytes,
                       actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
}

// Sends a response with the given return_status. This can be used to reply to a
// request received by |CheckRemoveReceived|.
void MultipleDeviceTestCase::SendRemoveReply(const zx::channel& remote) {
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_handles;
  // Write the RemoveDone message.
  memset(bytes, 0, sizeof(bytes));
  auto req = reinterpret_cast<fuchsia_device_manager_CoordinatorRemoveDoneRequest*>(bytes);
  req->hdr.ordinal = fuchsia_device_manager_CoordinatorRemoveDoneOrdinal;
  req->hdr.txid = 1;
  zx_status_t status =
      fidl_encode(&fuchsia_device_manager_CoordinatorRemoveDoneRequestTable, bytes, sizeof(*req),
                  handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*req), nullptr, 0);
  ASSERT_OK(status);

  coordinator_loop()->RunUntilIdle();

  // Verify the RemoveDone response.
  uint32_t actual_bytes;
  status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                       &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(0, actual_handles);

  fidl::EncodedMessage<::llcpp::fuchsia::device::manager::Coordinator::RemoveDoneResponse> encoded(
      fidl::BytePart(bytes, actual_bytes, actual_bytes));
  auto decode_result = fidl::Decode(std::move(encoded));
  ASSERT_OK(decode_result.status);

  const ::llcpp::fuchsia::device::manager::Coordinator::RemoveDoneResponse& msg =
      *decode_result.message.message();
  ASSERT_FALSE(msg.result.is_err());
}

void MultipleDeviceTestCase::CheckRemoveReceivedAndReply(const zx::channel& remote) {
  CheckRemoveReceived(remote);
  SendRemoveReply(remote);
}

class UnbindTestCase : public MultipleDeviceTestCase {
 public:
  // The expected action to receive. This is required as device_remove does not call unbind
  // on the initial device.
  enum class Action {
    kNone,
    kRemove,
    kUnbind,
  };

  struct DeviceDesc {
    // Index into the device desc array below.  UINT32_MAX = platform_bus()
    const size_t parent_desc_index;
    const char* const name;
    Action want_action = Action::kNone;
    // If non-null, will be run after receiving the Remove / Unbind request,
    // but before replying.
    std::function<void()> unbind_op = nullptr;
    // index for use with device()
    size_t index = 0;
    bool removed = false;
    bool unbound = false;
  };
  // |target_device_index| is the index of the device in the |devices| array to
  // schedule removal of.
  // If |unbind_children_only| is true, it will skip removal of the target device.
  void UnbindTest(DeviceDesc devices[], size_t num_devices, size_t target_device_index,
                  bool unbind_children_only = false, bool unbind_target_device = false);
};

TEST_F(UnbindTestCase, UnbindLeaf) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"}, {UINT32_MAX, "root_child2"},
      {0, "root_child1_1"},        {0, "root_child1_2"},
      {2, "root_child1_1_1"},      {1, "root_child2_1", Action::kRemove},
  };
  // Only remove root_child2_1.
  size_t index_to_remove = 5;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

TEST_F(UnbindTestCase, UnbindMultipleChildren) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kRemove}, {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},        {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},      {1, "root_child2_1"},
  };
  // Remove root_child1 and all its children.
  size_t index_to_remove = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

// This tests the removal of a child device in unbind. e.g.
//
// void MyDevice::Unbind() {
//   child->DdkRemove();
//   DdkRemove();
// }
TEST_F(UnbindTestCase, UnbindWithRemoveOp) {
  // Remove root_child1 and all its children.
  size_t index_to_remove = 0;
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kRemove},
      {0, "root_child1_1", Action::kUnbind},
      {1, "root_child1_1_1", Action::kRemove},
      {2, "root_child1_1_1_1", Action::kUnbind},
  };

  // We will schedule child device 1_1_1's removal in device 1_1's unbind hook.
  auto unbind_op = [&] {
    ASSERT_NO_FATAL_FAILURES(
        coordinator_.ScheduleDevhostRequestedRemove(device(devices[2].index)->device));
  };
  devices[1].unbind_op = unbind_op;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

TEST_F(UnbindTestCase, UnbindChildrenOnly) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"},  // Unbinding children of this device.
      {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},
      {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},
      {1, "root_child2_1"},
  };
  // Remove the children of root_child1.
  size_t target_device_index = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), target_device_index,
                                      true /* unbind_children_only */));
}

TEST_F(UnbindTestCase, UnbindSelf) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kUnbind},  // Require unbinding of the target device.
      {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},
      {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},
      {1, "root_child2_1"},
  };
  // Unbind root_child1.
  size_t index_to_remove = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove,
                                      false /* unbind_children_only */,
                                      true /* unbind_target_device */));
}

void UnbindTestCase::UnbindTest(DeviceDesc devices[], size_t num_devices,
                                size_t target_device_index, bool unbind_children_only,
                                bool unbind_target_device) {
  size_t num_to_remove = 0;
  size_t num_to_unbind = 0;
  for (size_t i = 0; i < num_devices; i++) {
    auto& desc = devices[i];
    fbl::RefPtr<devmgr::Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus();
    } else {
      size_t index = devices[desc.parent_desc_index].index;
      parent = device(index)->device;
    }
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, desc.name, 0 /* protocol id */, "", &desc.index));
    if (desc.want_action == Action::kUnbind) {
      num_to_unbind++;
      num_to_remove++;
    } else if (desc.want_action == Action::kRemove) {
      num_to_remove++;
    }
  }

  auto& desc = devices[target_device_index];
  if (unbind_children_only) {
    // Skip removal of the target device.
    ASSERT_NO_FATAL_FAILURES(
        coordinator_.ScheduleDevhostRequestedUnbindChildren(device(desc.index)->device));
  } else {
    ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleDevhostRequestedRemove(device(desc.index)->device,
                                                                         unbind_target_device));
  }
  coordinator_loop()->RunUntilIdle();

  while (num_to_unbind > 0) {
    bool made_progress = false;
    // Currently devices are unbound from the ancestor first.
    // Always check from leaf device upwards, so we ensure no child
    // is unbound before its parent.
    // To avoid overflow, check the counter before it is decremented.
    for (size_t i = num_devices; i-- > 0;) {
      auto& desc = devices[i];
      if (desc.unbound) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }
      ASSERT_NE(desc.want_action, Action::kNone);
      if (desc.want_action == Action::kUnbind) {
        ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(device(desc.index)->remote));
        if (desc.unbind_op) {
          desc.unbind_op();
        }
        ASSERT_NO_FATAL_FAILURES(SendUnbindReply(device(desc.index)->remote));
        desc.unbound = true;
      }
      // Check if the parent is expected to have been unbound already.
      if (desc.parent_desc_index != UINT32_MAX) {
        auto parent_desc = devices[desc.parent_desc_index];
        if (parent_desc.want_action == Action::kUnbind) {
          ASSERT_TRUE(parent_desc.unbound);
        }
      }

      --num_to_unbind;
      made_progress = true;
    }
    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  // Now check that we receive the removals in the expected order, leaf first.
  while (num_to_remove > 0) {
    bool made_progress = false;
    for (size_t i = 0; i < num_devices; ++i) {
      auto& desc = devices[i];
      if (desc.removed) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }

      ASSERT_NE(desc.want_action, Action::kNone);
      ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(desc.index)->remote));

      // Check that all our children have already been removed.
      for (size_t j = 0; j < num_devices; ++j) {
        auto& other_desc = devices[j];
        if (other_desc.parent_desc_index == i) {
          ASSERT_TRUE(other_desc.removed);
        }
      }

      desc.removed = true;
      --num_to_remove;
      made_progress = true;
    }

    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  for (size_t i = 0; i < num_devices; i++) {
    auto& desc = devices[i];
    ASSERT_NULL(device(desc.index)->device->GetActiveUnbind());
    ASSERT_NULL(device(desc.index)->device->GetActiveRemove());
  }
}

TEST_F(UnbindTestCase, UnbindSysDevice) {
  // Since the sys device is immortal, only its children will be unbound.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(platform_bus_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(platform_bus_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(sys_proxy_remote_));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator_.sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator_.sys_device()->GetActiveRemove());
}

TEST_F(UnbindTestCase, UnbindWhileRemovingProxy) {
  // The unbind task should complete immediately.
  // The remove task is blocked on the platform bus remove task completing.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()->proxy()));

  // Since the sys device is immortal, only its children will be unbound.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(platform_bus_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(platform_bus_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(sys_proxy_remote_));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator_.sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator_.sys_device()->GetActiveRemove());
}

TEST_F(UnbindTestCase, NumRemovals) {
  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  // Make sure the coordinator device does not detect the devhost's channel closing,
  // otherwise it will try to remove an already dead device and we will get a log error.
  child_device->remote.reset();
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(child_device->device->num_removal_attempts(), 1);
}

TEST_F(UnbindTestCase, AddDuringParentUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the request until we add the device.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceived(parent_device->remote));

  // Adding a child device to an unbinding parent should fail.
  fbl::RefPtr<devmgr::Device> child;
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  ASSERT_OK(status);
  status = coordinator_.AddDevice(parent_device->device, std::move(local), nullptr /* props_data */,
                                  0 /* props_count */, "child", 0 /* protocol_id */,
                                  nullptr /* driver_path */, nullptr /* args */,
                                  false /* invisible */, zx::channel() /* client_remote */, &child);
  ASSERT_NOT_OK(status);
  coordinator_loop()->RunUntilIdle();

  // Complete the original parent unbind.
  ASSERT_NO_FATAL_FAILURES(SendRemoveReply(parent_device->remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, TwoConcurrentRemovals) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  // Schedule concurrent removals.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, ManyConcurrentRemovals) {
  size_t num_devices = 100;
  size_t idx_map[num_devices];

  for (size_t i = 0; i < num_devices; i++) {
    auto parent = i == 0 ? platform_bus() : device(idx_map[i - 1])->device;
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, "child", 0 /* protocol id */, "", &idx_map[i]));
  }

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(idx_map[i])->device));
  }

  coordinator_loop()->RunUntilIdle();

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(
        CheckRemoveReceivedAndReply(device(idx_map[num_devices - i - 1])->remote));
    coordinator_loop()->RunUntilIdle();
  }
}

TEST_F(UnbindTestCase, ForcedRemovalDuringUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the unbind request.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(child_device->remote));

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->remote = zx::channel();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(devmgr::Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(devmgr::Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());
}

TEST_F(UnbindTestCase, ForcedRemovalDuringRemove) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the remove request.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceived(child_device->remote));

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->remote = zx::channel();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(devmgr::Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(devmgr::Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(child_device->device->GetActiveRemove());
}

TEST_F(UnbindTestCase, RemoveParentWhileRemovingChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  // Add a grandchild so that the child's remove task does not begin running after the
  // child's unbind task completes.
  size_t grandchild_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(child_device->device, "grandchild", 0 /* protocol id */, "", &grandchild_index));

  auto* grandchild_device = device(grandchild_index);

  // Start removing the child. Since we are not requesting an unbind
  // the unbind task will complete immediately. The remove task will be waiting
  // on the grandchild's remove to complete.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  // Start removing the parent.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(grandchild_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(grandchild_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, RemoveParentAndChildSimultaneously) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleDevhostRequestedRemove(parent_device->device,
                                                                       false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // At the same time, have the child try to remove itself.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleDevhostRequestedRemove(child_device->device,
                                                                       false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // The child device will not reply, as it already called device_remove previously.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->remote));
  coordinator_loop()->RunUntilIdle();
}

class SuspendTestCase : public MultipleDeviceTestCase {
 public:
  void SuspendTest(uint32_t flags);
  void StateTest(zx_status_t suspend_status, devmgr::Device::State want_device_state);
};

TEST_F(SuspendTestCase, Poweroff) {
  ASSERT_NO_FATAL_FAILURES(SuspendTest(DEVICE_SUSPEND_FLAG_POWEROFF));
}

TEST_F(SuspendTestCase, Reboot) {
  ASSERT_NO_FATAL_FAILURES(SuspendTest(DEVICE_SUSPEND_FLAG_REBOOT));
}

TEST_F(SuspendTestCase, RebootWithFlags) {
  ASSERT_NO_FATAL_FAILURES(SuspendTest(DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER));
}

TEST_F(SuspendTestCase, Mexec) { ASSERT_NO_FATAL_FAILURES(SuspendTest(DEVICE_SUSPEND_FLAG_MEXEC)); }

TEST_F(SuspendTestCase, SuspendToRam) {
  ASSERT_NO_FATAL_FAILURES(SuspendTest(DEVICE_SUSPEND_FLAG_SUSPEND_RAM));
}

// Verify the suspend order is correct
void SuspendTestCase::SuspendTest(uint32_t flags) {
  struct DeviceDesc {
    // Index into the device desc array below.  UINT32_MAX = platform_bus()
    const size_t parent_desc_index;
    const char* const name;
    // index for use with device()
    size_t index = 0;
    bool suspended = false;
  };
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"}, {UINT32_MAX, "root_child2"}, {0, "root_child1_1"},
      {0, "root_child1_2"},        {2, "root_child1_1_1"},      {1, "root_child2_1"},
  };
  for (auto& desc : devices) {
    fbl::RefPtr<devmgr::Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus();
    } else {
      size_t index = devices[desc.parent_desc_index].index;
      parent = device(index)->device;
    }
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, desc.name, 0 /* protocol id */, "", &desc.index));
  }

  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  size_t num_to_suspend = fbl::count_of(devices);
  while (num_to_suspend > 0) {
    // Check that platform bus is not suspended yet.
    ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_remote()));

    bool made_progress = false;
    // Since the table of devices above is topologically sorted (i.e.
    // any child is below its parent), this loop should always be able
    // to catch a parent receiving a suspend message before its child.
    for (size_t i = 0; i < fbl::count_of(devices); ++i) {
      auto& desc = devices[i];
      if (desc.suspended) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }

      ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(desc.index)->remote, flags, ZX_OK));

      // Make sure all descendants of this device are already suspended.
      // We just need to check immediate children since this will
      // recursively enforce that property.
      for (auto& other_desc : devices) {
        if (other_desc.parent_desc_index == i) {
          ASSERT_TRUE(other_desc.suspended);
        }
      }

      desc.suspended = true;
      --num_to_suspend;
      made_progress = true;
    }

    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(platform_bus_remote(), flags, ZX_OK));
}

TEST_F(SuspendTestCase, SuspendSuccess) {
  ASSERT_NO_FATAL_FAILURES(StateTest(ZX_OK, devmgr::Device::State::kSuspended));
}

TEST_F(SuspendTestCase, SuspendFail) {
  ASSERT_NO_FATAL_FAILURES(StateTest(ZX_ERR_BAD_STATE, devmgr::Device::State::kActive));
}

// Verify the device transitions in and out of the suspending state.
void SuspendTestCase::StateTest(zx_status_t suspend_status,
                                devmgr::Device::State want_device_state) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "device", 0 /* protocol id */, "", &index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  // Check for the suspend message without replying.
  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(index)->remote, flags));

  ASSERT_EQ(device(index)->device->state(), devmgr::Device::State::kSuspending);

  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(index)->remote, suspend_status));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(device(index)->device->state(), want_device_state);
}

class CompositeTestCase : public MultipleDeviceTestCase {
 public:
  ~CompositeTestCase() override = default;

  void CheckCompositeCreation(const char* composite_name, const size_t* device_indexes,
                              size_t device_indexes_count, size_t* component_indexes_out,
                              zx::channel* composite_remote_out);

 protected:
  void SetUp() override {
    MultipleDeviceTestCase::SetUp();
    ASSERT_NOT_NULL(coordinator_.component_driver());
  }
};

TEST_F(MultipleDeviceTestCase, UnbindThenSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // The child should be unbound first.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(device(child_index)->remote));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(device(child_index)->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->remote));
  coordinator_loop()->RunUntilIdle();

  // The suspend task should complete but not send a suspend message.
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->remote));

  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(platform_bus_remote(), flags, ZX_OK));
}

TEST_F(MultipleDeviceTestCase, SuspendThenUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(child_index)->remote, flags));
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // Check that the child device has not yet started unbinding.
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->remote));

  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(child_index)->remote, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The parent should have started suspending. Don't reply yet.
  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(parent_index)->remote, flags));

  // Finish unbinding the child.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(child_index)->remote));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->remote));
  coordinator_loop()->RunUntilIdle();

  // Finish suspending the parent.
  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(parent_index)->remote, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(platform_bus_remote(), flags, ZX_OK));

  // The parent should now be removed.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->remote));
  coordinator_loop()->RunUntilIdle();
}

void CompositeTestCase::CheckCompositeCreation(const char* composite_name,
                                               const size_t* device_indexes,
                                               size_t device_indexes_count,
                                               size_t* component_indexes_out,
                                               zx::channel* composite_remote_out) {
  for (size_t i = 0; i < device_indexes_count; ++i) {
    auto device_state = device(device_indexes[i]);
    // Check that the components got bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_state->remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    char name[32];
    snprintf(name, sizeof(name), "%s-comp-device-%zu", composite_name, i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(device_state->device, name, 0, driver, &component_indexes_out[i]));
  }
  // Make sure the composite comes up
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), composite_name, device_indexes_count, composite_remote_out));
}

class CompositeAddOrderTestCase : public CompositeTestCase {
 public:
  enum class AddLocation {
    // Add the composite before any components
    BEFORE,
    // Add the composite after some components
    MIDDLE,
    // Add the composite after all components
    AFTER,
  };
  void ExecuteTest(AddLocation add);
};

class CompositeAddOrderSharedComponentTestCase : public CompositeAddOrderTestCase {
 public:
  enum class DevNum {
    DEV1 = 1,
    DEV2,
  };
  void ExecuteSharedComponentTest(AddLocation dev1Add, AddLocation dev2Add);
};

void CompositeAddOrderSharedComponentTestCase::ExecuteSharedComponentTest(AddLocation dev1_add,
                                                                          AddLocation dev2_add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDev1Name = "composite-dev1";
  const char* kCompositeDev2Name = "composite-dev2";
  auto do_add = [&](const char* devname) {
    ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
        platform_bus(), protocol_id, fbl::count_of(protocol_id), nullptr /* props */, 0, devname));
  };

  if (dev1_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
  }

  if (dev2_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && dev1_add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
    }
    if (i == 0 && dev2_add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
    }
  }

  if (dev1_add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
  }

  zx::channel composite_remote1;
  zx::channel composite_remote2;
  size_t component_device1_indexes[fbl::count_of(device_indexes)];
  size_t component_device2_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev1Name, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device1_indexes, &composite_remote1));
  if (dev2_add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev2Name, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device2_indexes, &composite_remote2));
}

void CompositeAddOrderTestCase::ExecuteTest(AddLocation add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  auto do_add = [&]() {
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                     nullptr /* props */, 0, kCompositeDevName));
  };

  if (add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add());
    }
  }

  if (add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  zx::channel composite_remote;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device_indexes, &composite_remote));
}
TEST_F(CompositeAddOrderTestCase, DefineBeforeDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderTestCase, DefineAfterDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::AFTER));
}

TEST_F(CompositeAddOrderTestCase, DefineInbetweenDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1BeforeDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1BeforeDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1MiddleDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1MiddleDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::MIDDLE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1AfterDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::AFTER, AddLocation::AFTER));
}

TEST_F(CompositeTestCase, CantAddFromNonPlatformBus) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "test-device", 0, "", &index));
  auto device_state = device(index);

  uint32_t protocol_id[] = {ZX_PROTOCOL_I2C, ZX_PROTOCOL_GPIO};
  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(device_state->device, protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, "composite-dev", ZX_ERR_ACCESS_DENIED));
}

TEST_F(CompositeTestCase, AddMultipleSharedComponentCompositeDevices) {
  size_t device_indexes[2];
  zx_status_t status = ZX_OK;
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                     nullptr /* props */, 0, composite_dev_name));
  }

  zx::channel composite_remote[5];
  size_t component_device_indexes[5][fbl::count_of(device_indexes)];
  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        CheckCompositeCreation(composite_dev_name, device_indexes, fbl::count_of(device_indexes),
                               component_device_indexes[i - 1], &composite_remote[i - 1]));
  }
  auto device1 = device(device_indexes[1])->device;
  size_t count = 0;
  for (auto& child : device1->children()) {
    count++;
    char name[32];
    snprintf(name, sizeof(name), "composite-dev-%zu-comp-device-1", count);
    if (strcmp(child.name().data(), name)) {
      status = ZX_ERR_INTERNAL;
    }
  }
  ASSERT_OK(status);
  ASSERT_EQ(count, 5);
}

TEST_F(CompositeTestCase, SharedComponentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDev1Name = "composite-dev-1";
  const char* kCompositeDev2Name = "composite-dev-2";
  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, kCompositeDev1Name));

  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, kCompositeDev2Name));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }
  zx::channel composite1_remote;
  zx::channel composite2_remote;
  size_t component_device1_indexes[fbl::count_of(device_indexes)];
  size_t component_device2_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev1Name, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device1_indexes, &composite1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev2Name, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device2_indexes, &composite2_remote));
  coordinator_loop()->RunUntilIdle();

  {
    auto device1 = device(device_indexes[1])->device;
    fbl::RefPtr<devmgr::Device> comp_device1;
    fbl::RefPtr<devmgr::Device> comp_device2;
    for (auto& comp : device1->components()) {
      auto comp_device = comp.composite()->device();
      if (!strcmp(comp_device->name().data(), kCompositeDev1Name)) {
        comp_device1 = comp_device;
        continue;
      }
      if (!strcmp(comp_device->name().data(), kCompositeDev2Name)) {
        comp_device2 = comp_device;
        continue;
      }
    }
    ASSERT_NOT_NULL(comp_device1);
    ASSERT_NOT_NULL(comp_device2);
  }
  // Remove device 0 and its children (component and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  zx::channel& device_remote = device(device_indexes[0])->remote;
  zx::channel& component1_remote = device(component_device1_indexes[0])->remote;
  zx::channel& component2_remote = device(component_device2_indexes[0])->remote;

  // Check the components have received their unbind requests.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(component1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(component2_remote));

  // The device and composites should not have received any requests yet.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite1_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite2_remote));

  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(component1_remote));
  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(component2_remote));
  coordinator_loop()->RunUntilIdle();

  // The composites should start unbinding since the components finished unbinding.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite2_remote));
  coordinator_loop()->RunUntilIdle();

  // We are still waiting for the composites to be removed.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component1_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component2_remote));

  // Finish removing the composites.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite2_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));

  // Finish removing the components.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component2_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device_remote));

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_state->remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev1-comp-device-0", 0,
                                       driver, &component_device1_indexes[0]));
  }
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_state->remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev2-comp-device-0", 0,
                                       driver, &component_device2_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDev1Name, fbl::count_of(device_indexes), &composite1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDev2Name, fbl::count_of(device_indexes), &composite2_remote));
}

TEST_F(CompositeTestCase, ComponentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }
  zx::channel composite_remote;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device_indexes, &composite_remote));
  coordinator_loop()->RunUntilIdle();

  {
    auto device1 = device(device_indexes[1])->device;
    fbl::RefPtr<devmgr::Device> comp_device;
    for (auto& comp : device1->components()) {
      comp_device = comp.composite()->device();
      if (!strcmp(comp_device->name().data(), kCompositeDevName)) {
        break;
      }
    }
    ASSERT_NOT_NULL(comp_device);
  }
  // Remove device 0 and its children (component and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  zx::channel& device_remote = device(device_indexes[0])->remote;
  zx::channel& component_remote = device(component_device_indexes[0])->remote;

  // The device and composite should not have received an unbind request yet.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite_remote));

  // Check the component and composite are unbound.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite_remote));
  coordinator_loop()->RunUntilIdle();

  // Still waiting for the composite to be removed.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  // Finish removing the composite.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));

  // Finish removing the component.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device_remote));
  coordinator_loop()->RunUntilIdle();

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(CheckBindDriverReceived(device_state->remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "component-device-0", 0, driver,
                                       &component_device_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDevName, fbl::count_of(device_indexes), &composite_remote));
}

TEST_F(CompositeTestCase, SuspendOrder) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device_indexes, &composite_remote));

  const uint32_t suspend_flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(suspend_flags));

  // Make sure none of the components have received their suspend requests
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_remote()));
  for (auto idx : device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  for (auto idx : component_device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  // The composite should have been the first to get one
  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(composite_remote, suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Next, all of the internal component devices should have them, but none of the devices
  // themselves
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_remote()));
  for (auto idx : device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  for (auto idx : component_device_indexes) {
    ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(idx)->remote, suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Next, the devices should get them
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_remote()));
  for (auto idx : device_indexes) {
    ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(device(idx)->remote, suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Finally, the platform bus driver, which is the parent of all of the devices
  ASSERT_NO_FATAL_FAILURES(CheckSuspendReceived(platform_bus_remote(), suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

// Make sure we receive devfs notifications when composite devices appear
TEST_F(CompositeTestCase, DevfsNotifications) {
  zx::channel watcher;
  {
    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &watcher, &remote));
    ASSERT_OK(devfs_watch(coordinator()->root_device()->self, std::move(remote),
                          fuchsia_io_WATCH_MASK_ADDED));
  }

  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device_indexes, &composite_remote));

  uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
  uint32_t msg_len = 0;
  ASSERT_OK(watcher.read(0, msg, nullptr, sizeof(msg), 0, &msg_len, nullptr));
  ASSERT_EQ(msg_len, 2 + strlen(kCompositeDevName));
  ASSERT_EQ(msg[0], fuchsia_io_WATCH_EVENT_ADDED);
  ASSERT_EQ(msg[1], strlen(kCompositeDevName));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(kCompositeDevName), msg + 2, msg[1]);
}

// Make sure the path returned by GetTopologicalPath is accurate
TEST_F(CompositeTestCase, Topology) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  fbl::count_of(device_indexes),
                                                  component_device_indexes, &composite_remote));

  devmgr::Devnode* dn = coordinator()->root_device()->self;
  fbl::RefPtr<devmgr::Device> composite_dev;
  ASSERT_OK(devmgr::devfs_walk(dn, "composite-dev", &composite_dev));

  char path_buf[PATH_MAX];
  ASSERT_OK(coordinator()->GetTopologicalPath(composite_dev, path_buf, sizeof(path_buf)));
  ASSERT_STR_EQ(path_buf, "/dev/composite-dev");
}

// Disable the test as it is flaking fxb/34842
TEST_F(MultipleDeviceTestCase, SuspendFidlMexec) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  async::Loop devhost_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  ASSERT_OK(devhost_loop.StartThread("DevHostLoop"));

  async::Wait suspend_task_pbus(
      platform_bus_remote().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        CheckSuspendReceived(platform_bus_remote(), DEVICE_SUSPEND_FLAG_MEXEC, ZX_OK);
      });
  ASSERT_OK(suspend_task_pbus.Begin(devhost_loop.dispatcher()));

  async::Wait suspend_task_sys(
      sys_proxy_remote_.get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        CheckSuspendReceived(sys_proxy_remote_, DEVICE_SUSPEND_FLAG_MEXEC, ZX_OK);
      });
  ASSERT_OK(suspend_task_sys.Begin(devhost_loop.dispatcher()));

  zx::channel services, services_remote;
  ASSERT_OK(zx::channel::create(0, &services, &services_remote));

  ASSERT_OK(coordinator()->BindOutgoingServices(std::move(services_remote)));

  zx::channel channel, channel_remote;
  ASSERT_OK(zx::channel::create(0, &channel, &channel_remote));

  const char* service = "svc/" fuchsia_device_manager_Administrator_Name;
  ASSERT_OK(fdio_service_connect_at(services.get(), service, channel_remote.release()));

  bool callback_executed = false;
  DoSuspend(DEVICE_SUSPEND_FLAG_MEXEC, [&](uint32_t flags) {
    zx_status_t call_status = ZX_OK;
    ASSERT_OK(fuchsia_device_manager_AdministratorSuspend(channel.get(), flags, &call_status));
    ASSERT_OK(call_status);
    callback_executed = true;
  });

  ASSERT_TRUE(callback_executed);
  ASSERT_FALSE(suspend_task_pbus.is_pending());
  ASSERT_FALSE(suspend_task_sys.is_pending());
}

TEST_F(MultipleDeviceTestCase, SuspendFidlMexecFail) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  async::Loop devhost_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  ASSERT_OK(devhost_loop.StartThread("DevHostLoop"));

  async::Wait suspend_task_pbus(
      platform_bus_remote().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        CheckSuspendReceived(platform_bus_remote(), DEVICE_SUSPEND_FLAG_MEXEC);
      });
  ASSERT_OK(suspend_task_pbus.Begin(devhost_loop.dispatcher()));

  async::Wait suspend_task_sys(
      sys_proxy_remote_.get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        CheckSuspendReceived(sys_proxy_remote_, DEVICE_SUSPEND_FLAG_MEXEC, ZX_OK);
      });
  ASSERT_OK(suspend_task_sys.Begin(devhost_loop.dispatcher()));

  zx::channel services, services_remote;
  ASSERT_OK(zx::channel::create(0, &services, &services_remote));

  ASSERT_OK(coordinator()->BindOutgoingServices(std::move(services_remote)));

  zx::channel channel, channel_remote;
  ASSERT_OK(zx::channel::create(0, &channel, &channel_remote));

  const char* service = "svc/" fuchsia_device_manager_Administrator_Name;
  ASSERT_OK(fdio_service_connect_at(services.get(), service, channel_remote.release()));

  bool callback_executed = false;
  DoSuspend(DEVICE_SUSPEND_FLAG_MEXEC, [&](uint32_t flags) {
    zx_status_t call_status = ZX_OK;
    ASSERT_OK(fuchsia_device_manager_AdministratorSuspend(channel.get(), flags, &call_status));
    ASSERT_EQ(call_status, ZX_ERR_TIMED_OUT);
    callback_executed = true;
  });

  ASSERT_TRUE(callback_executed);
  ASSERT_FALSE(suspend_task_pbus.is_pending());
  ASSERT_TRUE(suspend_task_sys.is_pending());
}

}  // namespace

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
