// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_
#define SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_

#include <fuchsia/hardware/tpmimpl/llcpp/fidl.h>
#include <lib/ddk/device.h>

#include <condition_variable>

#include <ddktl/device.h>

#include "src/devices/tpm/drivers/tpm/commands.h"

namespace tpm {

using TpmCommandCallback = fit::function<void(zx_status_t, TpmResponseHeader*)>;
struct TpmCommand {
  std::unique_ptr<TpmCmdHeader> cmd;
  TpmCommandCallback handler;
};

class TpmDevice;
using DeviceType = ddk::Device<TpmDevice, ddk::Initializable, ddk::Suspendable, ddk::Unbindable>;

class TpmDevice : public DeviceType {
 public:
  TpmDevice(zx_device_t* parent, fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> client)
      : DeviceType(parent), tpm_(std::move(client)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // DDK mixins
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() {
    if (command_thread_.joinable()) {
      command_thread_.join();
    }
    delete this;
  }
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);

 private:
  template <typename CmdType>
  void QueueCommand(std::unique_ptr<CmdType> cmd, TpmCommandCallback&& callback)
      __TA_EXCLUDES(command_mutex_);
  zx_status_t DoCommand(TpmCommand& cmd);
  void CommandThread(ddk::InitTxn txn);
  zx_status_t ReadFromFifo(cpp20::span<uint8_t> data);
  zx_status_t DoInit();

  fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> tpm_;
  std::vector<TpmCommand> command_queue_ __TA_GUARDED(command_mutex_);
  std::mutex command_mutex_;
  std::condition_variable_any command_ready_;
  std::thread command_thread_;
  std::optional<ddk::UnbindTxn> unbind_txn_ __TA_GUARDED(command_mutex_);
  bool shutdown_ __TA_GUARDED(command_mutex_) = false;
};

}  // namespace tpm

#endif  // SRC_DEVICES_TPM_DRIVERS_TPM_TPM_H_
