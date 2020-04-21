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

class MockFshostAdminServer final : public llcpp::fuchsia::fshost::Admin::Interface {
 public:
  MockFshostAdminServer() : has_been_shutdown_(false) {}

  std::unique_ptr<llcpp::fuchsia::fshost::Admin::SyncClient> CreateClient(
      async_dispatcher* dispatcher) {
    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
      return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(zx::channel());
    }

    status = fidl::Bind(dispatcher, std::move(server), this);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to create client for mock fshost admin, failed to bind: %s",
           zx_status_get_string(status));
      return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(zx::channel());
    }

    return std::make_unique<llcpp::fuchsia::fshost::Admin::SyncClient>(std::move(client));
  }

  void Shutdown(ShutdownCompleter::Sync completer) override {
    has_been_shutdown_ = true;
    completer.Reply();
  }

  bool has_been_shutdown_;
};

class CoordinatorForTest : public Coordinator {
 public:
  CoordinatorForTest(CoordinatorConfig config) : Coordinator(std::move(config)) {}

  void SetFshostAdminClient(std::unique_ptr<llcpp::fuchsia::fshost::Admin::SyncClient> client) {
    fshost_admin_client_ = std::move(client);
  }

  MockFshostAdminServer admin_server_;
};

struct DeviceState {
  DeviceState() = default;
  DeviceState(DeviceState&& other)
      : device(std::move(other.device)),
        coordinator_remote(std::move(other.coordinator_remote)),
        controller_remote(std::move(other.controller_remote)) {}

  DeviceState& operator=(DeviceState&& other) {
    device = std::move(other.device);
    coordinator_remote = std::move(other.coordinator_remote);
    controller_remote = std::move(other.controller_remote);
    return *this;
  }

  ~DeviceState() {
    if (device) {
      device->coordinator->RemoveDevice(device, false);
    }
  }
  // The representation in the coordinator of the device
  fbl::RefPtr<Device> device;
  // The remote end of the channel that the coordinator is talking to
  zx::channel coordinator_remote;
  // The remote end of the channel that the controller is talking to
  zx::channel controller_remote;
};

class MultipleDeviceTestCase : public zxtest::Test {
 public:
  ~MultipleDeviceTestCase() override = default;

  async::Loop* coordinator_loop() { return &coordinator_loop_; }
  bool coordinator_loop_thread_running() { return coordinator_loop_thread_running_; }
  void set_coordinator_loop_thread_running(bool value) { coordinator_loop_thread_running_ = value; }
  CoordinatorForTest* coordinator() { return &coordinator_; }

  const fbl::RefPtr<Devhost>& devhost() { return devhost_; }
  const zx::channel& devhost_remote() { return devhost_remote_; }

  const fbl::RefPtr<Device>& platform_bus() const { return platform_bus_.device; }
  const zx::channel& platform_bus_coordinator_remote() const {
    return platform_bus_.coordinator_remote;
  }
  const zx::channel& platform_bus_controller_remote() const {
    return platform_bus_.controller_remote;
  }
  DeviceState* device(size_t index) const { return &devices_[index]; }

  void AddDevice(const fbl::RefPtr<Device>& parent, const char* name, uint32_t protocol_id,
                 fbl::String driver, bool invisible, bool has_init, bool reply_to_init,
                 bool always_init, size_t* device_index);
  void AddDevice(const fbl::RefPtr<Device>& parent, const char* name, uint32_t protocol_id,
                 fbl::String driver, size_t* device_index);
  void RemoveDevice(size_t device_index);

  bool DeviceHasPendingMessages(size_t device_index);
  bool DeviceHasPendingMessages(const zx::channel& remote);

  void DoSuspend(uint32_t flags);
  void DoSuspend(uint32_t flags, fit::function<void(uint32_t)> suspend_cb);
  void DoSuspendWithCallback(uint32_t flags, fit::function<void(zx_status_t)> suspend_complete_cb);

  void DoResume(
      SystemPowerState target_state, ResumeCallback callback = [](zx_status_t) {});
  void DoResume(SystemPowerState target_state, fit::function<void(SystemPowerState)> resume_cb);

  void CheckInitReceived(const zx::channel& remote, zx_txid_t* txid);
  void SendInitReply(const zx::channel& remote, zx_txid_t txid, zx_status_t return_status = ZX_OK);
  void CheckInitReceivedAndReply(const zx::channel& remote, zx_status_t return_status = ZX_OK);

  void CheckUnbindReceived(const zx::channel& remote, zx_txid_t* txid);
  void SendUnbindReply(const zx::channel& remote, zx_txid_t txid);
  void CheckUnbindReceivedAndReply(const zx::channel& remote);
  void CheckRemoveReceived(const zx::channel& remote, zx_txid_t* zxid);
  void SendRemoveReply(const zx::channel& remote, zx_txid_t txid);
  void CheckRemoveReceivedAndReply(const zx::channel& remote);

  void CheckSuspendReceived(const zx::channel& remote, uint32_t expected_flags, zx_txid_t* txid);
  void SendSuspendReply(const zx::channel& remote, zx_status_t return_status, zx_txid_t txid);
  void CheckSuspendReceivedAndReply(const zx::channel& remote, uint32_t expected_flags,
                                    zx_status_t return_status);
  void CheckCreateDeviceReceived(const zx::channel& remote, const char* expected_driver,
                                 zx::channel* device_coordinator_remote,
                                 zx::channel* device_controller_remote);
  void CheckResumeReceived(const zx::channel& remote, SystemPowerState target_state,
                           zx_txid_t* txid);
  void SendResumeReply(const zx::channel& remote, zx_status_t return_status, zx_txid_t txid);
  void CheckResumeReceived(const zx::channel& remote, SystemPowerState target_state,
                           zx_status_t return_status);

 protected:
  void SetUp() override;
  void TearDown() override;

  // These should be listed after devhost/sys_proxy as it needs to be
  // destroyed before them.
  async::Loop coordinator_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  bool coordinator_loop_thread_running_ = false;

  mock_boot_arguments::Server boot_args_{{}};
  llcpp::fuchsia::boot::Arguments::SyncClient args_client_{zx::channel()};

  // The admin/bootargs servers need their own loop/thread, because if we schedule them
  // on coordinator_loop then coordinator will deadlock waiting
  // for itself to respond to its requests.
  async::Loop mock_server_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};

  CoordinatorForTest coordinator_{DefaultConfig(
      coordinator_loop_.dispatcher(), mock_server_loop_.dispatcher(), &boot_args_, &args_client_)};

  // The fake devhost that the platform bus is put into
  fbl::RefPtr<Devhost> devhost_;

  // The remote end of the channel that the coordinator uses to talk to the
  // devhost
  zx::channel devhost_remote_;

  // The remote end of the channel that the coordinator uses to talk to the
  // sys device proxy
  zx::channel sys_proxy_coordinator_remote_;
  zx::channel sys_proxy_controller_remote_;

  // The device object representing the platform bus driver (child of the
  // sys proxy)
  DeviceState platform_bus_;

  // A list of all devices that were added during this test, and their
  // channels.  These exist to keep them alive until the test is over.
  fbl::Vector<DeviceState> devices_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_MULTIPLE_DEVICE_TEST_H_
