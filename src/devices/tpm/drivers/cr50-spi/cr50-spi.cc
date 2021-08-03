// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/cr50-spi/cr50-spi.h"

#include <fuchsia/hardware/spi/cpp/banjo.h>
#include <fuchsia/hardware/tpmimpl/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include "src/devices/lib/acpi/client.h"
#include "src/devices/tpm/drivers/cr50-spi/cr50-spi_bind.h"

namespace cr50::spi {

// The best resource for how this driver should work
// is the Cr50 TPM source code. It's available here:
// https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/cr50_stab/chip/g/spp_tpm.c

zx_status_t Cr50SpiDevice::Create(void *ctx, zx_device_t *parent) {
  auto acpi = acpi::Client::Create(parent);

  if (acpi.is_error()) {
    zxlogf(ERROR, "Failed to get ACPI client: %s", acpi.status_string());
    return acpi.error_value();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_spi::Device>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  ddk::SpiProtocolClient spi(parent, "spi000");
  if (!spi.is_valid()) {
    zxlogf(ERROR, "Could not find SPI fragment");
    return ZX_ERR_NOT_FOUND;
  }

  spi.ConnectServer(endpoints->server.TakeChannel());

  fidl::WireSyncClient<fuchsia_hardware_spi::Device> client(std::move(endpoints->client));
  auto dev = std::make_unique<Cr50SpiDevice>(parent, std::move(acpi.value()), std::move(client));
  return dev->Bind(&dev);
}

zx_status_t Cr50SpiDevice::Bind(std::unique_ptr<Cr50SpiDevice> *dev) {
  auto result = acpi_.borrow().MapInterrupt(0);
  if (!result.ok() || result->result.is_err()) {
    zxlogf(WARNING, "Failed to get IRQ: %s",
           result.ok() ? zx_status_get_string(result->result.err())
                       : result.FormatDescription().data());
  } else {
    irq_ = std::move(result->result.mutable_response().irq);
    irq_thread_ = std::thread(&Cr50SpiDevice::IrqThread, this);
  }

  zx_status_t status = loop_.StartThread("cr50-spi-fidl-thread");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start FIDL thread: %d", status);
    return status;
  }

  auto can_assert = spi_.CanAssertCs();
  if (!can_assert.ok()) {
    zxlogf(ERROR, "Failed to send FIDL request to SPI driver: %s",
           can_assert.FormatDescription().data());
    return can_assert.status();
  }
  if (!can_assert->can) {
    zxlogf(
        ERROR,
        "cr50-spi needs the ability to explicitly assert and deassert CS, which is not supported.");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = DdkAdd(ddk::DeviceAddArgs("cr50-spi").set_inspect_vmo(inspect_.DuplicateVmo()));
  __UNUSED auto unused = dev->release();
  return status;
}

void Cr50SpiDevice::DdkInit(ddk::InitTxn txn) {
  // Post onto the FIDL thread, since nobody will be trying to do FIDL transactions until we reply
  // to the InitTxn anyway.
  async::PostTask(loop_.dispatcher(), [this, txn = std::move(txn)]() mutable {
    LogFirmwareVersion();
    txn.Reply(ZX_OK);
  });
}

void Cr50SpiDevice::DdkUnbind(ddk::UnbindTxn txn) {
  unbind_txn_ = std::move(txn);
  sync_completion_signal(&unbind_txn_ready_);
  irq_.destroy();
  if (!irq_thread_.joinable()) {
    // If the IRQ thread is not joinable, it was probably never started.
    // Reply ourselves.
    unbind_txn_->Reply();
    unbind_txn_ = std::nullopt;
  }
}

void Cr50SpiDevice::DdkRelease() {
  if (irq_thread_.joinable()) {
    irq_thread_.join();
  }
  delete this;
}

void Cr50SpiDevice::TpmImplConnectServer(zx::channel server) {
  fidl::BindServer(loop_.dispatcher(),
                   fidl::ServerEnd<fuchsia_hardware_tpmimpl::TpmImpl>(std::move(server)), this);
}

void Cr50SpiDevice::Read(ReadRequestView request, ReadCompleter::Sync &completer) {
  // Cr50 ignores locality, so we do too. See section 33 of
  // https://trustedcomputinggroup.org/wp-content/uploads/TCG_TPM2_r1p59_Part1_Architecture_pub.pdf.
  fidl::Arena<fuchsia_hardware_tpmimpl::wire::kTpmMaxDataTransfer> alloc;
  if (request->count > fuchsia_hardware_tpmimpl::wire::kTpmMaxDataTransfer) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  WaitForReady();
  fidl::VectorView<uint8_t> buffer(alloc, request->count);
  auto result = DoXfer(request->address, buffer, false);
  if (result.is_error()) {
    completer.ReplyError(result.error_value());
  } else {
    completer.ReplySuccess(buffer);
  }
}

void Cr50SpiDevice::Write(WriteRequestView request, WriteCompleter::Sync &completer) {
  WaitForReady();
  auto result = DoXfer(request->address, request->data, true);
  if (result.is_error()) {
    completer.ReplyError(result.error_value());
  } else {
    completer.ReplySuccess();
  }
}

void Cr50SpiDevice::LogFirmwareVersion() {
  static constexpr uint32_t kTpmFwVersionReg = 0x00000F90;
  // Start reading the firmware version.
  // First, write nothing to the register so we go back to the start of the FW version.
  uint8_t empty[1] = {0};
  auto view = fidl::VectorView<uint8_t>::FromExternal(empty);
  auto status = DoXfer(kTpmFwVersionReg, view, true);
  if (status.is_error()) {
    zxlogf(ERROR, "failed to get tpm version :(");
    return;
  }

  // Read in 32-byte chunks.
  uint8_t fw_version[96] = {0};
  uint8_t chunk[32] = {0};
  size_t non_null_bytes = 0;
  size_t i = 0;
  do {
    non_null_bytes = 0;
    view = fidl::VectorView<uint8_t>::FromExternal(chunk);
    WaitForReady();
    status = DoXfer(kTpmFwVersionReg, view, false);
    if (status.is_error()) {
      zxlogf(ERROR, "failed to read firmware version: %s", status.status_string());
      return;
    }

    for (size_t j = 0; j < countof(chunk); j++) {
      fw_version[i] = chunk[j];
      i++;
      non_null_bytes++;
      if (chunk[j] == 0 || i >= countof(fw_version))
        break;
    }
  } while (non_null_bytes == countof(chunk) && i < countof(fw_version));

  zxlogf(INFO, "TPM firmware version: %s", fw_version);
  // Add an inspect node with the firmware version.
  inspect_.GetRoot().CreateString("fw-version", std::string(reinterpret_cast<char *>(fw_version)),
                                  &inspect_);
}

void Cr50SpiDevice::IrqThread() {
  while (true) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to wait for IRQ: %d", status);
      break;
    }

    sync_completion_signal(&tpm_ready_);
  }

  sync_completion_wait(&unbind_txn_ready_, ZX_TIME_INFINITE);
  if (unbind_txn_ != std::nullopt) {
    unbind_txn_->Reply();
    unbind_txn_ = std::nullopt;
  }
}

void Cr50SpiDevice::WaitForReady() {
  static constexpr zx_duration_t kReadyTimeout = ZX_MSEC(750);  // TPM_TIMEOUT_A
  if (irq_.is_valid()) {
    zx_status_t status = sync_completion_wait(&tpm_ready_, kReadyTimeout);
    sync_completion_reset(&tpm_ready_);
    if (status != ZX_OK) {
      zxlogf(WARNING, "timeout waiting for tpm");
    }
  } else {
    // Sleep for 2ms, b/80481396
    zx::nanosleep(zx::deadline_after(zx::msec(2)));
  }
}

void Cr50SpiDevice::WakeUp() {
  static constexpr zx::duration kSleepTime(ZX_MSEC(1000));
  zx::time sleep_time = last_command_time_ + kSleepTime;
  if (zx::clock::get_monotonic() >= sleep_time) {
    zxlogf(INFO, "asleep for too long, waking up!");
    // Wake the cr50 by asserting CS.
    auto result = spi_.AssertCs();
    if (!result.ok() || result->status != ZX_OK) {
      zxlogf(
          ERROR, "Failed to assert SPI CS to wakeup cr50: %s",
          result.ok() ? zx_status_get_string(result->status) : result.FormatDescription().data());
    }

    auto deassert = spi_.DeassertCs();
    if (!deassert.ok() || deassert->status != ZX_OK) {
      zxlogf(ERROR, "Failed to deassert SPI CS to wakeup cr50: %s",
             deassert.ok() ? zx_status_get_string(deassert->status)
                           : deassert.FormatDescription().data());
    }

    // Let the H1 wake up.
    zx::nanosleep(zx::deadline_after(zx::usec(100)));
  }

  last_command_time_ = zx::clock::get_monotonic();
}

zx::status<> Cr50SpiDevice::SendHeader(uint16_t address, size_t msg_length, bool writing) {
  // Start the transaction with the 4-byte magic header required by the TPM SPI protocol.
  uint8_t header[4];
  header[0] = msg_length - 1;
  if (!writing) {
    header[0] |= 0x80;
  }
  header[1] = 0xd4;  // Addresses are always '0xd4xxxx'
  header[2] = (address >> 8) & 0xff;
  header[3] = address & 0xff;
  auto result = spi_.ExchangeVector(fidl::VectorView<uint8_t>::FromExternal(header));
  if (!result.ok()) {
    zxlogf(ERROR, "send FIDL request failed: %s", result.FormatDescription().data());
    return zx::error(result.status());
  }
  if (result->status != ZX_OK) {
    zxlogf(ERROR, "spi xfer failed: %s", zx_status_get_string(result->status));
    return zx::error(result->status);
  }

  // The TPM will send back a 0x1 in the last byte if it's ready, otherwise we have to do flow
  // control.
  uint8_t ready = result->rxdata[3] & 0x1;
  if (!ready) {
    return FlowControl();
  }
  return zx::ok();
}

zx::status<> Cr50SpiDevice::FlowControl() {
  static constexpr zx::duration kFlowControlTimeout(ZX_MSEC(750 /* TPM_TIMEOUT_A */));
  // The TPM isn't ready until we get back a 1 in the last bit.
  // The Cr50 in practice always does at least 1 byte of flow control.
  auto deadline = zx::deadline_after(kFlowControlTimeout);
  uint8_t ready = 0;
  while (!ready && zx::clock::get_monotonic() < deadline) {
    auto result = spi_.ReceiveVector(1);
    if (!result.ok()) {
      zxlogf(ERROR, "send FIDL request failed: %s", result.FormatDescription().data());
      return zx::error(result.status());
    }
    if (result->status != ZX_OK) {
      zxlogf(ERROR, "spi xfer failed: %s", zx_status_get_string(result->status));
      return zx::error(result->status);
    }

    if (result->data.count() != 1) {
      zxlogf(ERROR, "spi returned incorrect number of bytes: %zu", result->data.count());
      return zx::error(ZX_ERR_INTERNAL);
    }
    ready = result->data[0] & 0x1;
  }
  return zx::ok();
}

zx::status<> Cr50SpiDevice::DoSpiWrite(fidl::VectorView<uint8_t> &buf) {
  auto result = spi_.TransmitVector(buf);
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::make_status(result->status);
}

zx::status<> Cr50SpiDevice::DoSpiRead(fidl::VectorView<uint8_t> &buf) {
  auto ret_vec = spi_.ReceiveVector(buf.count());
  if (!ret_vec.ok()) {
    return zx::error(ret_vec.status());
  }
  if (ret_vec->status != ZX_OK) {
    return zx::error(ret_vec->status);
  }

  // Put returned data in the output buffer.
  memcpy(buf.mutable_data(), ret_vec->data.data(), ret_vec->data.count());
  return zx::ok();
}

zx::status<> Cr50SpiDevice::DoXfer(uint16_t address, fidl::VectorView<uint8_t> &buf,
                                   bool do_write) {
  zxlogf(DEBUG, "%sing %zu bytes at 0x%x", do_write ? "writ" : "read", buf.count(), address);
  WakeUp();
  auto assert = spi_.AssertCs();
  if (!assert.ok() || assert->status != ZX_OK) {
    zxlogf(ERROR, "asserting spi bus failed");
    return zx::error(ZX_ERR_UNAVAILABLE);
  }
  auto deasserter = fit::defer([this]() { spi_.DeassertCs(); });

  auto status = SendHeader(address, buf.count(), do_write);
  if (status.is_error()) {
    return status.take_error();
  }

  // TPM is ready - do the actual exchange.
  if (do_write) {
    status = DoSpiWrite(buf);
  } else {
    status = DoSpiRead(buf);
  }

  return status;
}

static const zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Cr50SpiDevice::Create;
  return ops;
}();
}  // namespace cr50::spi

// clang-format off
ZIRCON_DRIVER(cr50-spi, cr50::spi::driver_ops, "zircon", "0.1");
