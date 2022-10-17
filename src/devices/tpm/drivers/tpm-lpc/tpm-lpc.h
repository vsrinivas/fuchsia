// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TPM_DRIVERS_TPM_LPC_TPM_LPC_H_
#define SRC_DEVICES_TPM_DRIVERS_TPM_LPC_TPM_LPC_H_

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <fuchsia/hardware/tpmimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <ddktl/unbind-txn.h>
#include <fbl/mutex.h>

#include "src/devices/lib/acpi/client.h"

namespace tpm::lpc {

// Defines a TPM LPC driver specified at:
// The device contains the base address for the TIS interface 0xfed40000 and
// the size of the MMIO area (0x5000). The device is poll mode only due to no
// unused IRQ being available on QEMU.
class TpmLpc;
using DeviceType = ddk::Device<TpmLpc, ddk::Messageable<fuchsia_hardware_tpmimpl::TpmImpl>::Mixin>;
class TpmLpc : public DeviceType, public ddk::TpmImplProtocol<TpmLpc, ddk::base_protocol> {
 public:
  explicit TpmLpc(zx_device_t* parent, acpi::Client acpi, fdf::MmioBuffer mmio)
      : DeviceType(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        acpi_(std::move(acpi)),
        mmio_(std::move(mmio)) {}
  virtual ~TpmLpc() = default;

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind(std::unique_ptr<TpmLpc>* dev);
  void DdkRelease();

  zx::result<> PerformTransfer(uint16_t address, fidl::VectorView<uint8_t>& buf, bool do_write);

  // Setup the TPM FIDL Server.
  void TpmImplConnectServer(zx::channel server);

  // FIDL methods.
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;

 private:
  async::Loop loop_;
  acpi::Client acpi_;
  fdf::MmioBuffer mmio_;
  inspect::Inspector inspect_;
  fbl::Mutex device_lock_;
};

}  // namespace tpm::lpc

#endif  // SRC_DEVICES_TPM_DRIVERS_TPM_LPC_TPM_LPC_H_
