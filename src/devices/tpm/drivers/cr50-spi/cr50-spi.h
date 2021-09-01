// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_TPM_DRIVERS_CR50_SPI_CR50_SPI_H_
#define SRC_DEVICES_TPM_DRIVERS_CR50_SPI_CR50_SPI_H_

#include <fidl/fuchsia.hardware.spi/cpp/wire.h>
#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/tpmimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/inspect/cpp/inspector.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace cr50::spi {

class Cr50SpiDevice;
using DeviceType = ddk::Device<Cr50SpiDevice, ddk::Initializable, ddk::Unbindable,
                               ddk::Messageable<fuchsia_hardware_tpmimpl::TpmImpl>::Mixin>;

class Cr50SpiDevice : public DeviceType,
                      public ddk::TpmImplProtocol<Cr50SpiDevice, ddk::base_protocol> {
 public:
  Cr50SpiDevice(zx_device_t* parent, acpi::Client acpi,
                fidl::WireSyncClient<fuchsia_hardware_spi::Device> spi)
      : DeviceType(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        acpi_(std::move(acpi)),
        spi_(std::move(spi)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Bind(std::unique_ptr<Cr50SpiDevice>* device_ptr);

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void TpmImplConnectServer(zx::channel server);

  // FIDL methods.
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;

  // For unit tests.
  inspect::Inspector& inspect() { return inspect_; }

 private:
  void LogFirmwareVersion();

  // Transfer |buf| to the TPM. If |do_write| is true, |buf| will be written to |address|,
  // otherwise it will be populated with data read from |address|.
  zx::status<> DoXfer(uint16_t address, fidl::VectorView<uint8_t>& buf, bool do_write);
  // Send the TPM header transaction header. This will also call FlowControl().
  zx::status<> SendHeader(uint16_t address, size_t msg_length, bool writing);
  // Do flow control after sending the header while waiting for the device to become ready.
  zx::status<> FlowControl();

  zx::status<> DoSpiWrite(fidl::VectorView<uint8_t>& buf);
  zx::status<> DoSpiRead(fidl::VectorView<uint8_t>& buf);

  void IrqThread();
  // Wait for the cr50 to become ready after sending a previous command.
  void WaitForReady();
  // Wake up the cr50 if it has gone idle.
  void WakeUp();

  async::Loop loop_;
  acpi::Client acpi_;
  fidl::WireSyncClient<fuchsia_hardware_spi::Device> spi_;
  zx::interrupt irq_;
  std::thread irq_thread_;
  sync_completion_t tpm_ready_;
  inspect::Inspector inspect_;

  zx::time last_command_time_ = zx::time::infinite_past();

  sync_completion_t unbind_txn_ready_;
  std::optional<ddk::UnbindTxn> unbind_txn_;
};

}  // namespace cr50::spi

#endif  // SRC_DEVICES_TPM_DRIVERS_CR50_SPI_CR50_SPI_H_
