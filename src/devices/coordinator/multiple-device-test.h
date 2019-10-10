// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DRIVER_FRAMEWORK_MULTIPLE_DEVICE_TEST_H_
#define SRC_DRIVER_FRAMEWORK_MULTIPLE_DEVICE_TEST_H_

#include <zxtest/zxtest.h>

#include "coordinator-test-utils.h"

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
  void set_coordinator_loop_thread_running(bool value) { coordinator_loop_thread_running_ = value; }
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

  void DoResume(SystemPowerState target_state);
  void DoResume(SystemPowerState target_state, fit::function<void(SystemPowerState)> resume_cb);

  void CheckUnbindReceived(const zx::channel& remote);
  void SendUnbindReply(const zx::channel& remote);
  void CheckUnbindReceivedAndReply(const zx::channel& remote);
  void CheckRemoveReceived(const zx::channel& remote);
  void SendRemoveReply(const zx::channel& remote);
  void CheckRemoveReceivedAndReply(const zx::channel& remote);
  void CheckSuspendReceived(const zx::channel& remote, uint32_t expected_flags);
  void SendSuspendReply(const zx::channel& remote, zx_status_t return_status);
  void CheckSuspendReceived(const zx::channel& remote, uint32_t expected_flags,
                            zx_status_t return_status);
  void CheckCreateDeviceReceived(const zx::channel& remote, const char* expected_driver,
                                 zx::channel* device_remote);
  void CheckResumeReceived(const zx::channel& remote, SystemPowerState target_state);
  void SendResumeReply(const zx::channel& remote, zx_status_t return_status);
  void CheckResumeReceived(const zx::channel& remote, SystemPowerState target_state,
                           zx_status_t return_status);

 protected:
  void SetUp() override;
  void TearDown() override;

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

#endif  // SRC_DRIVER_FRAMEWORK_MULTIPLE_DEVICE_TEST_H_
