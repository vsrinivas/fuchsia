// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_LPC_CHROMIUMOS_EC_LPC_H_
#define SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_LPC_CHROMIUMOS_EC_LPC_H_

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/svc/outgoing.h>

#include <ddktl/device.h>

#include "chromiumos-platform-ec/ec_commands.h"

namespace chromiumos_ec_lpc {

class ChromiumosEcLpc;
using DeviceType = ddk::Device<ChromiumosEcLpc, ddk::Initializable, ddk::Unbindable,
                               ddk::Messageable<fuchsia_hardware_google_ec::Device>::Mixin>;
class ChromiumosEcLpc : public DeviceType {
 public:
  explicit ChromiumosEcLpc(zx_device_t* parent)
      : DeviceType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  virtual ~ChromiumosEcLpc() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // FIDL methods.
  void RunCommand(RunCommandRequestView request, RunCommandCompleter::Sync& completer) override;

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  std::optional<svc::Outgoing> outgoing_;
  std::mutex io_lock_;
  async::Loop loop_;
};

}  // namespace chromiumos_ec_lpc

#endif  // SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_LPC_CHROMIUMOS_EC_LPC_H_
