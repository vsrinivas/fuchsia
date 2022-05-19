// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi-dev/dev-ec.h"

#include <lib/ddk/hw/inout.h>
#include <zircon/errors.h>

#include <ddktl/device.h>

namespace acpi_ec {
namespace {
class RealIoPort : public IoPortInterface {
 public:
  uint8_t inp(uint16_t port) override { return ::inp(port); }

  void outp(uint16_t port, uint8_t value) override { ::outp(port, value); }

  zx_status_t Map(uint16_t port) override {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    return zx_ioports_request(get_root_resource(), port, 1);
  }

  ~RealIoPort() override = default;
};
}  // namespace

zx_status_t EcDevice::Create(zx_device_t* parent, acpi::Acpi* acpi, ACPI_HANDLE handle) {
  auto device = std::make_unique<EcDevice>(parent, acpi, handle, std::make_unique<RealIoPort>());
  zx_status_t status = device->Init();
  if (status == ZX_OK) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = device.release();
    zxlogf(INFO, "initialised acpi-ec");
  } else {
    zxlogf(ERROR, "Failed to init acpi-ec: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t EcDevice::Init() {
  // Do we need global lock?
  zx::status<bool> use_glk = NeedsGlobalLock();
  if (use_glk.is_error()) {
    return use_glk.error_value();
  }
  use_global_lock_ = *use_glk;
  inspect_.GetRoot().CreateBool("use-global-lock", use_global_lock_, &inspect_);

  // Create event.
  zx_status_t status = zx::event::create(0, &irq_);
  if (status != ZX_OK) {
    return status;
  }
  // Find GPE info.
  zx::status<std::pair<ACPI_HANDLE, uint32_t>> gpe_info = GetGpeInfo();
  if (gpe_info.is_error()) {
    return gpe_info.error_value();
  }
  gpe_info_ = *gpe_info;

  // Find I/O ports and map them.
  zx::status<> io_status = SetupIo();
  if (io_status.is_error()) {
    return io_status.error_value();
  }

  // Set up GPE handler.
  acpi::status<> gpe_status = acpi_->InstallGpeHandler(
      gpe_info->first, gpe_info->second, ACPI_GPE_EDGE_TRIGGERED, EcDevice::GpeHandlerThunk, this);
  if (gpe_status.is_error()) {
    return gpe_status.zx_status_value();
  }

  gpe_status = acpi_->EnableGpe(gpe_info->first, gpe_info->second);
  if (gpe_status.is_error()) {
    return gpe_status.zx_status_value();
  }

  // Start transaction thread -- some boards seem to call into the address space handler
  // from AML bytecode when you call InstallAddressSpaceHandler(), so we need to do this first.
  txn_thread_ = std::thread([this]() { TransactionThread(); });

  // Install address space handler.
  acpi::status<> addr_space_status = acpi_->InstallAddressSpaceHandler(
      handle_, ACPI_ADR_SPACE_EC, EcDevice::AddressSpaceThunk, nullptr, this);
  if (addr_space_status.is_error()) {
    return ZX_ERR_INTERNAL;
  }

  // Start query thread now that we're fully ready to service queries.
  query_thread_ = std::thread([this]() { QueryThread(); });

  status = DdkAdd(ddk::DeviceAddArgs("acpi-ec")
                      .set_proto_id(ZX_PROTOCOL_MISC)
                      .set_inspect_vmo(inspect_.DuplicateVmo()));
  return status;
}

void EcDevice::DdkUnbind(ddk::UnbindTxn txn) {
  irq_.signal(0, EcSignal::kEcShutdown);
  acpi::status<> status = acpi_->DisableGpe(gpe_info_.first, gpe_info_.second);
  if (status.is_error()) {
    zxlogf(WARNING, "Failed to disable GPE: %d", status.status_value());
  }
  status = acpi_->RemoveGpeHandler(gpe_info_.first, gpe_info_.second, EcDevice::GpeHandlerThunk);
  if (status.is_error()) {
    zxlogf(WARNING, "Failed to remove GPE handler: %d", status.status_value());
  }
  status =
      acpi_->RemoveAddressSpaceHandler(handle_, ACPI_ADR_SPACE_EC, EcDevice::AddressSpaceThunk);
  if (status.is_error()) {
    zxlogf(WARNING, "failed to remove address space handler: %d", status.status_value());
  }
  txn.Reply();
}

void EcDevice::HandleGpe() {
  uint8_t data = io_ports_->inp(cmd_port_);
  zx_signals_t pending = 0;
  zx_signals_t clear = 0;
  // IBF:1 = EC is yet to read last byte we wrote, so we can't write another.
  if ((data & EcStatus::kIbf)) {
    clear |= EcSignal::kCanWrite;
  } else {
    pending |= EcSignal::kCanWrite;
  }

  // OBF:1 = EC has some data ready for us to read.
  if ((data & EcStatus::kObf)) {
    pending |= EcSignal::kCanRead;
  } else {
    clear |= EcSignal::kCanRead;
  }

  // SCI_EVT:1 = EC wants us to run a query command.
  if ((data & EcStatus::kSciEvt)) {
    pending |= EcSignal::kPendingEvent;
  } else {
    clear |= EcSignal::kPendingEvent;
  }

  irq_.signal(clear, pending);
}

ACPI_STATUS EcDevice::SpaceRequest(uint32_t function, ACPI_PHYSICAL_ADDRESS paddr, uint32_t width,
                                   UINT64* value) {
  if (width != 8 && width != 16 && width != 32 && width != 64) {
    return AE_BAD_PARAMETER;
  }
  if (paddr > UINT8_MAX || paddr - 1 + (width / 8) > UINT8_MAX) {
    return AE_BAD_PARAMETER;
  }

  uint8_t addr = paddr & UINT8_MAX;

  uint8_t bytes = static_cast<uint8_t>(width / 8);
  uint8_t* value_bytes = reinterpret_cast<uint8_t*>(value);

  if (function == ACPI_WRITE) {
    for (uint8_t i = 0; i < bytes; i++) {
      zx_status_t status = Write(addr + i, value_bytes[i]);
      if (status != ZX_OK) {
        return AE_ERROR;
      }
    }
  } else {
    for (uint8_t i = 0; i < bytes; i++) {
      zx::status<uint8_t> result = Read(addr + i);
      if (result.is_error()) {
        return AE_ERROR;
      }
      value_bytes[i] = *result;
    }
  }

  return AE_OK;
}

void EcDevice::TransactionThread() {
  zx_signals_t pending = 0;
  do {
    zx::status<zx_signals_t> status = WaitForIrq(EcSignal::kTransactionReady);
    if (status.is_error()) {
      if (status.error_value() != ZX_ERR_CANCELED) {
        zxlogf(ERROR, "irq wait failed: %s", status.status_string());
      }
      break;
    }
    irq_.signal(EcSignal::kTransactionReady, 0);

    // Take the current transaction queue so we can operate on it.
    std::vector<Transaction*> txns;
    {
      std::scoped_lock lock(transaction_lock_);
      txns.swap(transaction_queue_);
    }

    // If we use the global lock, grab it before executing transactions.
    uint32_t global_lock;
    if (use_global_lock_) {
      acpi::status<uint32_t> ret = acpi_->AcquireGlobalLock(0xffff);
      if (ret.is_error()) {
        zxlogf(ERROR, "failed to acquire global lock: %d", ret.status_value());
        // Fail this batch of transactions.
        for (auto txn : txns) {
          txn->status = ret.zx_status_value();
          sync_completion_signal(&txn->done);
        }
        continue;
      }
      global_lock = *ret;
    }
    for (auto txn : txns) {
      txn->status = DoTransaction(txn);
      sync_completion_signal(&txn->done);
      finished_txns_.Add(1);
    }

    if (use_global_lock_) {
      acpi::status<> ret = acpi_->ReleaseGlobalLock(global_lock);
      if (ret.is_error()) {
        zxlogf(ERROR, "failed to release global lock: %d", ret.status_value());
        // Not a lot we can do here.
      }
    }

  } while ((pending & kEcShutdown) == 0);
}

zx_status_t EcDevice::DoTransaction(Transaction* txn) {
  // Clear "can write", so that we don't spuriously write data before the command has been received.
  irq_.signal(EcSignal::kCanWrite, 0);
  // Issue the command.
  io_ports_->outp(cmd_port_, txn->op);
  switch (txn->op) {
    case EcCmd::kRead: {
      // Wait until we can write the address.
      zx::status<zx_signals_t> status = WaitForIrq(EcSignal::kCanWrite);
      if (status.is_error()) {
        return status.error_value();
      }
      irq_.signal(EcSignal::kCanWrite, 0);

      // Specify the address
      io_ports_->outp(data_port_, txn->addr);

      // Wait until we can read the value.
      status = WaitForIrq(EcSignal::kCanRead);
      if (status.is_error()) {
        return status.error_value();
      }

      irq_.signal(EcSignal::kCanRead, 0);

      // Return the value.
      txn->value = io_ports_->inp(data_port_);
      break;
    }
    case EcCmd::kWrite: {
      // Wait until we can write the address.
      zx::status<zx_signals_t> status = WaitForIrq(EcSignal::kCanWrite);
      if (status.is_error()) {
        return status.error_value();
      }
      irq_.signal(EcSignal::kCanWrite, 0);

      // Specify the address
      io_ports_->outp(data_port_, txn->addr);

      // Wait until we can write the value.
      status = WaitForIrq(EcSignal::kCanWrite);
      if (status.is_error()) {
        return status.error_value();
      }

      irq_.signal(EcSignal::kCanWrite, 0);

      // Specify the value
      io_ports_->outp(data_port_, txn->value);

      // Wait for EC to read the value.
      status = WaitForIrq(EcSignal::kCanWrite);
      if (status.is_error()) {
        return status.error_value();
      }
      irq_.signal(EcSignal::kCanWrite, 0);

      break;
    }
    case EcCmd::kQuery: {
      // Wait for the EC to respond.
      zx::status<zx_signals_t> status = WaitForIrq(EcSignal::kCanRead);
      if (status.is_error()) {
        return status.error_value();
      }

      irq_.signal(EcSignal::kCanRead, 0);

      txn->value = io_ports_->inp(data_port_);
      break;
    }
  }

  return ZX_OK;
}

zx::status<zx_signals_t> EcDevice::WaitForIrq(zx_signals_t signals) {
  zx_signals_t pending;
  signals |= EcSignal::kEcShutdown;
  zx_status_t status = irq_.wait_one(signals, zx::time::infinite(), &pending);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  if (pending & EcSignal::kEcShutdown) {
    return zx::error(ZX_ERR_CANCELED);
  }

  return zx::ok(pending);
}

void EcDevice::QueryThread() {
  while (true) {
    zx::status<zx_signals_t> status = WaitForIrq(EcSignal::kPendingEvent);
    if (status.is_error()) {
      if (status.error_value() != ZX_ERR_CANCELED) {
        zxlogf(ERROR, "irq wait failed: %s", status.status_string());
      }
      break;
    }

    uint8_t event;
    while (io_ports_->inp(cmd_port_) & EcStatus::kSciEvt) {
      zx::status<uint8_t> ret = Query();
      if (ret.is_error()) {
        break;
      }
      event = *ret;
      if (event == 0) {
        break;
      }

      char method[5] = {0};
      snprintf(method, sizeof(method), "_Q%02x", event);
      last_query_.Set(method);
      // Don't care about return value.
      __UNUSED auto status = acpi_->EvaluateObject(handle_, method, std::nullopt);
    }

    irq_.signal(EcSignal::kPendingEvent, 0);
  }
}

zx_status_t EcDevice::Write(uint8_t addr, uint8_t val) {
  Transaction txn{
      .op = EcCmd::kWrite,
      .addr = addr,
      .value = val,
  };
  zx_status_t status = QueueTransactionAndWait(&txn);
  return status;
}

zx::status<uint8_t> EcDevice::Read(uint8_t addr) {
  Transaction txn{
      .op = EcCmd::kRead,
      .addr = addr,
  };
  zx_status_t status = QueueTransactionAndWait(&txn);
  if (status == ZX_OK) {
    return zx::ok(txn.value);
  }
  return zx::error(status);
}

zx::status<uint8_t> EcDevice::Query() {
  Transaction txn{
      .op = EcCmd::kQuery,
  };
  zx_status_t status = QueueTransactionAndWait(&txn);
  if (status == ZX_OK) {
    return zx::ok(txn.value);
  }
  return zx::error(status);
}

zx_status_t EcDevice::QueueTransactionAndWait(Transaction* txn) {
  {
    std::scoped_lock lock(transaction_lock_);
    transaction_queue_.emplace_back(txn);
    zx_status_t status = irq_.signal(0, EcSignal::kTransactionReady);
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to signal transaction ready");
      return status;
    }
  }
  sync_completion_wait(&txn->done, ZX_TIME_INFINITE);
  return txn->status;
}

zx::status<bool> EcDevice::NeedsGlobalLock() {
  acpi::status<acpi::UniquePtr<ACPI_OBJECT>> ret =
      acpi_->EvaluateObject(handle_, "_GLK", std::nullopt);
  if (ret.is_error()) {
    if (ret.error_value() == AE_NOT_FOUND) {
      // Not found means no global lock.
      return zx::ok(false);
    }
    zxlogf(ERROR, "EvaluateObject for _GLK failed: %d", ret.error_value());
    return zx::error(ret.zx_status_value());
  }

  auto obj = std::move(*ret);
  if (obj->Type != ACPI_TYPE_INTEGER) {
    zxlogf(ERROR, "_GLK had wrong type: %d", obj->Type);
    return zx::error(ZX_ERR_WRONG_TYPE);
  }
  return zx::ok(obj->Integer.Value != 0);
}

zx::status<std::pair<ACPI_HANDLE, uint32_t>> EcDevice::GetGpeInfo() {
  acpi::status<acpi::UniquePtr<ACPI_OBJECT>> ret =
      acpi_->EvaluateObject(handle_, "_GPE", std::nullopt);
  if (ret.is_error()) {
    return zx::error(ret.zx_status_value());
  }

  ACPI_HANDLE block = nullptr;
  uint32_t number = 0;

  /* According to section 12.11 of ACPI v6.1, a _GPE object on this device
   * evaluates to either an integer specifying bit in the GPEx_STS blocks
   * to use, or a package specifying which GPE block and which bit inside
   * that block to use. */
  if (ret->Type == ACPI_TYPE_INTEGER) {
    number = static_cast<uint32_t>(ret->Integer.Value);
  } else if (ret->Type == ACPI_TYPE_PACKAGE) {
    if (ret->Package.Count != 2) {
      return zx::error(ZX_ERR_WRONG_TYPE);
    }
    ACPI_OBJECT* block_obj = &ret->Package.Elements[0];
    ACPI_OBJECT* gpe_num_obj = &ret->Package.Elements[1];
    if (block_obj->Type != ACPI_TYPE_LOCAL_REFERENCE) {
      return zx::error(ZX_ERR_WRONG_TYPE);
    }
    if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
      return zx::error(ZX_ERR_WRONG_TYPE);
    }
    block = block_obj->Reference.Handle;
    number = static_cast<uint32_t>(gpe_num_obj->Integer.Value);
  } else {
    return zx::error(ZX_ERR_WRONG_TYPE);
  }

  return zx::ok(std::make_pair(block, number));
}

zx::status<> EcDevice::SetupIo() {
  size_t resource_count = 0;
  acpi::status<> ret = acpi_->WalkResources(
      handle_, "_CRS", [this, &resource_count](ACPI_RESOURCE* rsrc) -> acpi::status<> {
        if (rsrc->Type == ACPI_RESOURCE_TYPE_END_TAG) {
          return acpi::ok();
        }

        /* The spec says there will be at most 3 resources */
        if (resource_count >= 3) {
          return acpi::error(AE_BAD_DATA);
        }
        /* The third resource only exists on HW-Reduced platforms, which
         * we don't support at the moment. */
        if (resource_count == 2) {
          return acpi::error(AE_NOT_IMPLEMENTED);
        }

        /* The two resources we're expecting are both address regions.
         * First the data one, then the command one.  We assume they're
         * single IO ports. */
        if (rsrc->Type != ACPI_RESOURCE_TYPE_IO) {
          return acpi::error(AE_SUPPORT);
        }
        if (rsrc->Data.Io.Maximum != rsrc->Data.Io.Minimum) {
          return acpi::error(AE_SUPPORT);
        }

        uint16_t port = rsrc->Data.Io.Minimum;
        if (resource_count == 0) {
          data_port_ = port;
        } else {
          cmd_port_ = port;
        }

        resource_count++;
        return acpi::ok();
      });

  zx_status_t status = io_ports_->Map(data_port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-ec: Failed to map ec data port: %s", zx_status_get_string(status));
    return zx::error(status);
  }
  status = io_ports_->Map(cmd_port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi-ec: Failed to map ec cmd port: %s", zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::make_status(ret.zx_status_value());
}

}  // namespace acpi_ec
