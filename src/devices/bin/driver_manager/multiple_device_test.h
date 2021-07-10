// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_MULTIPLE_DEVICE_TEST_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_MULTIPLE_DEVICE_TEST_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/status.h>

#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "coordinator_test_utils.h"
#include "src/devices/lib/log/log.h"

namespace fdm = fuchsia_device_manager;

class MockFshostAdminServer final : public fidl::WireServer<fuchsia_fshost::Admin> {
 public:
  MockFshostAdminServer() : has_been_shutdown_(false) {}

  fidl::Client<fuchsia_fshost::Admin> CreateClient(async_dispatcher* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_fshost::Admin>();
    if (endpoints.is_error()) {
      return fidl::Client<fuchsia_fshost::Admin>();
    }

    auto status = fidl::BindSingleInFlightOnly(dispatcher, std::move(endpoints->server), this);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create client for mock fshost admin, failed to bind: %s",
           zx_status_get_string(status));
      return fidl::Client<fuchsia_fshost::Admin>();
    }

    return fidl::Client(std::move(endpoints->client), dispatcher);
  }

  void Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) override {
    has_been_shutdown_ = true;
    completer.Reply();
  }

  bool has_been_shutdown_;
};

class CoordinatorForTest {
 public:
  CoordinatorForTest(CoordinatorConfig config, async_dispatcher_t* dispatcher)
      : inspect_manager_(dispatcher),
        coordinator_(std::move(config), &inspect_manager_, dispatcher) {}

  Coordinator& coordinator() { return coordinator_; }

 private:
  InspectManager inspect_manager_;
  Coordinator coordinator_;
};

class DeviceState : public fidl::WireServer<fdm::DeviceController> {
 public:
  DeviceState() = default;
  DeviceState(DeviceState&& other)
      : device(std::move(other.device)),
        coordinator_client(std::move(other.coordinator_client)),
        controller_server(std::move(other.controller_server)) {}

  DeviceState& operator=(DeviceState&& other) {
    device = std::move(other.device);
    coordinator_client = std::move(other.coordinator_client);
    controller_server = std::move(other.controller_server);
    return *this;
  }

  ~DeviceState() {
    if (device) {
      device->coordinator->RemoveDevice(device, false);
    }
  }

  bool HasPendingMessages();

  void CheckBindDriverReceivedAndReply(std::string_view expected_driver_name);

  void CheckInitReceived();
  void SendInitReply(zx_status_t return_status = ZX_OK);
  void CheckInitReceivedAndReply(zx_status_t return_status = ZX_OK);

  void CheckUnbindReceived();
  void SendUnbindReply();
  void CheckUnbindReceivedAndReply();

  void CheckRemoveReceived();
  void SendRemoveReply();
  void CheckRemoveReceivedAndReply();

  void CheckSuspendReceived(uint32_t expected_flags);
  void SendSuspendReply(zx_status_t return_status);
  void CheckSuspendReceivedAndReply(uint32_t expected_flags, zx_status_t return_status);

  void CheckResumeReceived(SystemPowerState target_state);
  void SendResumeReply(zx_status_t return_status);
  void CheckResumeReceivedAndReply(SystemPowerState target_state, zx_status_t return_status);

  // The representation in the coordinator of the device
  fbl::RefPtr<Device> device;
  // The remote end of the channel that the coordinator is talking to
  fidl::ClientEnd<fdm::Coordinator> coordinator_client;
  // The remote end of the channel that the controller is talking to
  fidl::ServerEnd<fdm::DeviceController> controller_server;

 private:
  void Dispatch();

  void BindDriver(BindDriverRequestView request, BindDriverCompleter::Sync& completer) override {
    bind_driver_path_ = std::string(request->driver_path.get());
    bind_completer_ = completer.ToAsync();
  }
  void ConnectProxy(ConnectProxyRequestView request,
                    ConnectProxyCompleter::Sync& _completer) override {}
  void Init(InitRequestView request, InitCompleter::Sync& completer) override {
    init_completer_ = completer.ToAsync();
  }
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override {
    suspend_flags_ = request->flags;
    suspend_completer_ = completer.ToAsync();
  }
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& completer) override {
    resume_target_state_ = request->target_system_state;
    resume_completer_ = completer.ToAsync();
  }
  void Unbind(UnbindRequestView request, UnbindCompleter::Sync& completer) override {
    unbind_completer_ = completer.ToAsync();
  }
  void CompleteRemoval(CompleteRemovalRequestView request,
                       CompleteRemovalCompleter::Sync& completer) override {
    remove_completer_ = completer.ToAsync();
  }
  void CompleteCompatibilityTests(CompleteCompatibilityTestsRequestView request,
                                  CompleteCompatibilityTestsCompleter::Sync& _completer) override {}
  void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override {}

  std::optional<BindDriverCompleter::Async> bind_completer_;
  std::string bind_driver_path_;
  std::optional<InitCompleter::Async> init_completer_;
  uint32_t suspend_flags_;
  std::optional<SuspendCompleter::Async> suspend_completer_;
  uint32_t resume_target_state_;
  std::optional<ResumeCompleter::Async> resume_completer_;
  std::optional<UnbindCompleter::Async> unbind_completer_;
  std::optional<CompleteRemovalCompleter::Async> remove_completer_;
};

class MultipleDeviceTestCase : public zxtest::Test {
 public:
  static CoordinatorConfig CreateConfig(async_dispatcher_t* bootargs_dispatcher,
                                        mock_boot_arguments::Server* boot_args,
                                        fidl::WireSyncClient<fuchsia_boot::Arguments>* client,
                                        bool enable_ephemeral) {
    auto config = DefaultConfig(bootargs_dispatcher, boot_args, client);
    config.enable_ephemeral = enable_ephemeral;
    return config;
  }

  explicit MultipleDeviceTestCase(bool enable_ephemeral = false)
      : coordinator_for_test_(CreateConfig(mock_server_loop_.dispatcher(), &boot_args_,
                                           &args_client_, enable_ephemeral),
                              coordinator_loop_.dispatcher()) {}

  ~MultipleDeviceTestCase() override = default;

  async::Loop* coordinator_loop() { return &coordinator_loop_; }
  bool coordinator_loop_thread_running() { return coordinator_loop_thread_running_; }
  void set_coordinator_loop_thread_running(bool value) { coordinator_loop_thread_running_ = value; }
  Coordinator& coordinator() { return coordinator_for_test_.coordinator(); }
  MockFshostAdminServer& admin_server() { return admin_server_; }

  const fbl::RefPtr<DriverHost>& driver_host() { return driver_host_; }
  const fidl::ServerEnd<fdm::DevhostController>& driver_host_server() {
    return driver_host_server_;
  }

  DeviceState* sys_proxy() { return &sys_proxy_; }
  DeviceState* platform_bus() { return &platform_bus_; }
  DeviceState* device(size_t index) const { return &devices_[index]; }

  void AddDevice(const fbl::RefPtr<Device>& parent, const char* name, uint32_t protocol_id,
                 fbl::String driver, bool invisible, bool has_init, bool reply_to_init,
                 bool always_init, zx::vmo inspect, size_t* device_index);
  void AddDeviceSkipAutobind(const fbl::RefPtr<Device>& parent, const char* name,
                             uint32_t protocol_id, size_t* device_index);
  void AddDevice(const fbl::RefPtr<Device>& parent, const char* name, uint32_t protocol_id,
                 fbl::String driver, size_t* device_index);
  void RemoveDevice(size_t device_index);

  void DoSuspend(uint32_t flags);
  void DoSuspend(uint32_t flags, fit::function<void(uint32_t)> suspend_cb);
  void DoSuspendWithCallback(uint32_t flags, fit::function<void(zx_status_t)> suspend_complete_cb);

  void DoResume(
      SystemPowerState target_state, ResumeCallback callback = [](zx_status_t) {});
  void DoResume(SystemPowerState target_state, fit::function<void(SystemPowerState)> resume_cb);

  void CheckCreateDeviceReceived(const fidl::ServerEnd<fdm::DevhostController>& server,
                                 const char* expected_driver,
                                 fidl::ClientEnd<fdm::Coordinator>* device_coordinator_client,
                                 fidl::ServerEnd<fdm::DeviceController>* device_controller_server);

 protected:
  void SetUp() override;
  void TearDown() override;

  // These should be listed after driver_host/sys_proxy as it needs to be
  // destroyed before them.
  async::Loop coordinator_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  bool coordinator_loop_thread_running_ = false;

  mock_boot_arguments::Server boot_args_{{}};
  fidl::WireSyncClient<fuchsia_boot::Arguments> args_client_{};

  // The admin/bootargs servers need their own loop/thread, because if we schedule them
  // on coordinator_loop then coordinator will deadlock waiting
  // for itself to respond to its requests.
  async::Loop mock_server_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};

  CoordinatorForTest coordinator_for_test_;
  MockFshostAdminServer admin_server_;

  // The fake driver_host that the platform bus is put into
  fbl::RefPtr<DriverHost> driver_host_;

  // The remote end of the channel that the coordinator uses to talk to the
  // driver_host
  fidl::ServerEnd<fdm::DevhostController> driver_host_server_;

  // The remote end of the channel that the coordinator uses to talk to the
  // sys device proxy
  DeviceState sys_proxy_;

  // The device object representing the platform bus driver (child of the
  // sys proxy)
  DeviceState platform_bus_;

  // A list of all devices that were added during this test, and their
  // channels.  These exist to keep them alive until the test is over.
  fbl::Vector<DeviceState> devices_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_MULTIPLE_DEVICE_TEST_H_
