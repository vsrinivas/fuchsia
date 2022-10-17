// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/tpm-lpc/tpm-lpc.h"

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire_types.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/mmio/mmio-buffer.h>

#include <fbl/auto_lock.h>

#include "src/devices/lib/acpi/client.h"
#include "src/devices/tpm/drivers/tpm-lpc/tpm-lpc-bind.h"

namespace tpm::lpc {

zx_status_t TpmLpc::Create(void* ctx, zx_device_t* dev) {
  zxlogf(INFO, "Creating tpm-lpc driver.");
  auto acpi = acpi::Client::Create(dev);
  if (acpi.is_error()) {
    return acpi.error_value();
  }
  std::optional<fdf::MmioBuffer> mmio;
  auto mmio_result = acpi->borrow()->GetMmio(0);
  if (mmio_result->is_error()) {
    zxlogf(ERROR, "Failed to get MMIO offset from the ACPI.");
    return mmio_result->error_value();
  }
  auto& mmio_value = mmio_result->value()->mmio;
  zx_status_t status =
      fdf::MmioBuffer::Create(mmio_value.offset, mmio_value.size, std::move(mmio_value.vmo),
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map MMIO buffer.");
    return status;
  }

  auto driver = std::make_unique<TpmLpc>(dev, std::move(acpi.value()), *std::move(mmio));
  return driver->Bind(&driver);
}

zx_status_t TpmLpc::Bind(std::unique_ptr<TpmLpc>* driver) {
  zx_status_t status = loop_.StartThread("tpm-lpc-fidl-thread");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start FIDL thread: %d", status);
    return status;
  }
  status = DdkAdd(ddk::DeviceAddArgs("tpm-lpc").set_inspect_vmo(inspect_.DuplicateVmo()));
  if (status == ZX_OK) {
    __UNUSED auto ptr = driver->release();
  }
  return status;
}

void TpmLpc::DdkRelease() { delete this; }

// Memory is only addressable in 4 byte increments on the qemu-driver, so we have
// to split up any incoming requests into 4 byte segments in big-endian order.
zx::result<> TpmLpc::PerformTransfer(uint16_t address, fidl::VectorView<uint8_t>& buf,
                                     bool do_write) {
  if (address > mmio_.get_size() || buf.count() > mmio_.get_size()) {
    zxlogf(ERROR, "Cannot proceed address is out of range.");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (buf.count() + address > mmio_.get_size()) {
    zxlogf(ERROR, "Cannot proceed buffer too big.");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  constexpr size_t kAddressAlignment = 4;
  if (do_write) {
    size_t aligned_size = (buf.count() / kAddressAlignment) * kAddressAlignment;
    for (size_t offset = 0; offset < aligned_size; offset += kAddressAlignment) {
      uint32_t val = 0;
      uint8_t* chunk = (uint8_t*)&val;
      chunk[0] = buf[offset];
      chunk[1] = buf[offset + 1];
      chunk[2] = buf[offset + 2];
      chunk[3] = buf[offset + 3];
      mmio_.Write32(val, address);
    }
    size_t remainder = buf.count() % kAddressAlignment;
    if (remainder != 0) {
      size_t offset = buf.count() - remainder;
      for (size_t i = 0; i < remainder; i++) {
        mmio_.Write8(buf[offset + i], address);
      }
    }
  } else {
    size_t aligned_size = (buf.count() / kAddressAlignment) * kAddressAlignment;
    for (size_t offset = 0; offset < aligned_size; offset += kAddressAlignment) {
      uint32_t val = mmio_.Read32(address);
      uint8_t* chunk = (uint8_t*)&val;
      buf.data()[offset] = chunk[0];
      buf.data()[offset + 1] = chunk[1];
      buf.data()[offset + 2] = chunk[2];
      buf.data()[offset + 3] = chunk[3];
    }
    size_t remainder = buf.count() % kAddressAlignment;
    if (remainder != 0) {
      size_t offset = buf.count() - remainder;
      for (size_t i = 0; i < remainder; i++) {
        buf.data()[offset + i] = mmio_.Read8(address);
      }
    }
  }
  return zx::ok();
}

void TpmLpc::TpmImplConnectServer(zx::channel server) {
  fidl::BindServer(loop_.dispatcher(),
                   fidl::ServerEnd<fuchsia_hardware_tpmimpl::TpmImpl>(std::move(server)), this);
}

void TpmLpc::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  fbl::AutoLock lock(&device_lock_);
  fidl::Arena<fuchsia_hardware_tpmimpl::wire::kTpmMaxDataTransfer> alloc;
  if (request->count > fuchsia_hardware_tpmimpl::wire::kTpmMaxDataTransfer) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  fidl::VectorView<uint8_t> buffer(alloc, request->count);
  auto result = PerformTransfer(static_cast<uint16_t>(request->address), buffer, false);
  if (result.is_error()) {
    completer.ReplyError(result.error_value());
  } else {
    completer.ReplySuccess(buffer);
  }
}

void TpmLpc::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  fbl::AutoLock lock(&device_lock_);
  auto result = PerformTransfer(static_cast<uint16_t>(request->address), request->data, true);
  if (result.is_error()) {
    completer.ReplyError(result.error_value());
  } else {
    completer.ReplySuccess();
  }
}

static zx_driver_ops_t tpm_lpc_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TpmLpc::Create;
  return ops;
}();

}  // namespace tpm::lpc

ZIRCON_DRIVER(TpmLpc, tpm::lpc::tpm_lpc_driver_ops, "zircon", "0.1");
