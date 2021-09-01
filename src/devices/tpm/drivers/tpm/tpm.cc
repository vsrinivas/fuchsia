// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/tpm/tpm.h"

#include <fidl/fuchsia.hardware.tpmimpl/cpp/wire.h>
#include <fuchsia/hardware/tpmimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <mutex>

#include "src/devices/tpm/drivers/tpm/commands.h"
#include "src/devices/tpm/drivers/tpm/registers.h"
#include "src/devices/tpm/drivers/tpm/tpm_bind.h"

namespace tpm {
namespace {

template <typename CmdType>
std::unique_ptr<TpmCmdHeader> make_cmd_header(
    std::unique_ptr<CmdType, std::default_delete<CmdType>> &&p) {
  // To safely cast into a TpmCmdHeader, we need the cmdtype to start with a TpmCmdHeader.
  static_assert(offsetof(CmdType, hdr) == 0 && std::is_same<decltype(p->hdr), TpmCmdHeader>::value,
                "CmdType must start with a TpmCmdHeader");
  // CmdType must be trivially destructible. If it isn't the below cast to TpmCmdHeader will mean
  // that the destructor of the actual class is never called.
  static_assert(std::is_trivially_destructible<CmdType>::value,
                "CmdType must be trivially destructible");
  std::unique_ptr<TpmCmdHeader> header(reinterpret_cast<TpmCmdHeader *>(p.release()));
  return header;
}
}  // namespace

using fuchsia_hardware_tpmimpl::wire::RegisterAddress;

zx_status_t TpmDevice::Create(void *ctx, zx_device_t *parent) {
  ddk::TpmImplProtocolClient tpm(parent);
  if (!tpm.is_valid()) {
    zxlogf(ERROR, "Failed to get TPM impl!");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_tpmimpl::TpmImpl>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  tpm.ConnectServer(endpoints->server.TakeChannel());

  fidl::WireSyncClient<fuchsia_hardware_tpmimpl::TpmImpl> client(std::move(endpoints->client));

  auto device = std::make_unique<TpmDevice>(parent, std::move(client));
  zx_status_t status = device->DdkAdd(ddk::DeviceAddArgs("tpm")
                                          .set_inspect_vmo(device->inspect_.DuplicateVmo())
                                          .set_proto_id(ZX_PROTOCOL_TPM));
  if (status == ZX_OK) {
    __UNUSED auto unused = device.release();
  }
  return status;
}

void TpmDevice::DdkInit(ddk::InitTxn txn) {
  command_thread_ = std::thread(&TpmDevice::CommandThread, this, std::move(txn));
}

void TpmDevice::DdkSuspend(ddk::SuspendTxn txn) {
  std::unique_ptr<TpmShutdownCmd> cmd;

  switch (txn.suspend_reason()) {
    case DEVICE_SUSPEND_REASON_REBOOT:
    case DEVICE_SUSPEND_REASON_REBOOT_BOOTLOADER:
    case DEVICE_SUSPEND_REASON_REBOOT_RECOVERY:
    case DEVICE_SUSPEND_REASON_POWEROFF:
      cmd = std::make_unique<TpmShutdownCmd>(TPM_SU_CLEAR);
      break;
    case DEVICE_SUSPEND_REASON_SUSPEND_RAM:
      cmd = std::make_unique<TpmShutdownCmd>(TPM_SU_STATE);
      break;
    default:
      zxlogf(WARNING, "Unknown suspend state %d", txn.requested_state());
      txn.Reply(ZX_OK, DEV_POWER_STATE_D0);
      return;
  }

  ZX_DEBUG_ASSERT(cmd);

  QueueCommand(
      std::move(cmd), [txn = std::move(txn)](zx_status_t status, TpmResponseHeader *hdr) mutable {
        if (status != ZX_OK) {
          zxlogf(ERROR, "Error sending TPM shutdown command: %s", zx_status_get_string(status));
          txn.Reply(status, DEV_POWER_STATE_D0);
        } else {
          txn.Reply(ZX_OK, txn.requested_state());
        }
      });
}

void TpmDevice::DdkUnbind(ddk::UnbindTxn txn) {
  std::scoped_lock lock(command_mutex_);
  ZX_DEBUG_ASSERT(unbind_txn_ == std::nullopt);
  unbind_txn_ = std::move(txn);
  shutdown_ = true;
  command_ready_.notify_all();
  if (!command_thread_.joinable()) {
    unbind_txn_->Reply();
    unbind_txn_ = std::nullopt;
  }
}

void TpmDevice::GetDeviceId(GetDeviceIdRequestView request, GetDeviceIdCompleter::Sync &completer) {
  completer.ReplySuccess(vendor_id_, device_id_, revision_id_);
}

void TpmDevice::ExecuteVendorCommand(ExecuteVendorCommandRequestView request,
                                     ExecuteVendorCommandCompleter::Sync &completer) {
  auto cmd = std::make_unique<TpmVendorCmd>(
      kTpmVendorPrefix | request->command_code,
      cpp20::span<const uint8_t>(request->data.data(), request->data.count()));

  auto async = completer.ToAsync();
  QueueCommand(std::move(cmd),
               [async = std::move(async)](zx_status_t status, TpmResponseHeader *hdr) mutable {
                 if (status != ZX_OK) {
                   async.ReplyError(status);
                   return;
                 }

                 TpmVendorResponse *vendor = reinterpret_cast<TpmVendorResponse *>(hdr);
                 auto vector_view = fidl::VectorView<uint8_t>::FromExternal(
                     vendor->data, vendor->hdr.ResponseSize() - sizeof(vendor->hdr));
                 async.ReplySuccess(hdr->response_code, vector_view);
               });
}

void TpmDevice::CommandThread(ddk::InitTxn txn) {
  zx_status_t status = DoInit();
  txn.Reply(status);
  if (status != ZX_OK) {
    return;
  }

  while (true) {
    std::vector<TpmCommand> queue;
    {
      std::scoped_lock lock(command_mutex_);
      command_ready_.wait(command_mutex_, [&]() __TA_REQUIRES(command_mutex_) {
        return !command_queue_.empty() || shutdown_;
      });

      if (shutdown_) {
        break;
      }

      std::swap(command_queue_, queue);
    }

    for (auto &op : queue) {
      // If the call succeeds DoCommand calls the handler with the response content.
      zx_status_t status = DoCommand(op);
      if (status != ZX_OK) {
        op.handler(status, nullptr);
      }
    }
  }

  {
    std::scoped_lock lock(command_mutex_);

    // Drain the command queue and cancel all pending commands.
    for (auto &op : command_queue_) {
      op.handler(ZX_ERR_CANCELED, nullptr);
    }
    command_queue_.clear();

    if (unbind_txn_.has_value()) {
      unbind_txn_->Reply();
      unbind_txn_ = std::nullopt;
    }
  }
}

zx_status_t TpmDevice::DoInit() {
  StsReg sts;
  zx_status_t status = sts.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }
  if (sts.tpm_family() != TpmFamily::kTpmFamily20) {
    zxlogf(ERROR, "unsupported TPM family, expected 2.0");

    return ZX_ERR_NOT_SUPPORTED;
  }

  DidVidReg id;
  status = id.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }

  vendor_id_ = id.vendor_id();
  device_id_ = id.device_id();

  RevisionReg rev;
  status = rev.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }

  revision_id_ = rev.revision_id();

  inspect_.GetRoot().CreateUint("vendor-id", vendor_id_, &inspect_);
  inspect_.GetRoot().CreateUint("device-id", device_id_, &inspect_);
  inspect_.GetRoot().CreateUint("revision-id", revision_id_, &inspect_);

  return ZX_OK;
}

template <typename CmdType>
void TpmDevice::QueueCommand(std::unique_ptr<CmdType> cmd, TpmCommandCallback &&callback) {
  auto header(make_cmd_header(std::move(cmd)));
  std::scoped_lock lock(command_mutex_);
  if (shutdown_) {
    callback(ZX_ERR_CANCELED, nullptr);
  } else {
    command_queue_.emplace_back(TpmCommand{
        .cmd = std::move(header),
        .handler = std::move(callback),
    });
    command_ready_.notify_all();
  }
}

zx_status_t TpmDevice::DoCommand(TpmCommand &cmd) {
  // See section 5.5.2.2 of the client platform spec.
  zx_status_t status = StsReg().set_command_ready(1).WriteTo(tpm_);
  if (status != ZX_OK) {
    return status;
  }

  StsReg sts;
  do {
    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
  } while (!sts.command_ready());

  auto finish_command = fit::defer([this]() {
    zx_status_t status = StsReg().set_command_ready(1).WriteTo(tpm_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write to TPM while finishing command.");
      return;
    }
  });

  size_t command_size = betoh32(cmd.cmd->command_size);
  uint8_t *buf = reinterpret_cast<uint8_t *>(cmd.cmd.get());
  while (command_size > 1) {
    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
    size_t burst_count = sts.burst_count();
    size_t burst = std::min(burst_count, command_size - 1);
    zxlogf(DEBUG, "Writing burst of %zu bytes, burst_count = %zu cmd_size = %zu", burst,
           burst_count, command_size);
    if (burst == 0) {
      zxlogf(WARNING, "TPM burst is zero when it shouldn't be.");
      continue;
    }

    auto view = fidl::VectorView<uint8_t>::FromExternal(buf, burst);
    auto result = tpm_.Write(0, RegisterAddress::kTpmDataFifo, view);
    if (!result.ok()) {
      zxlogf(ERROR, "FIDL call failed!");
      return result.status();
    }
    if (result->result.is_err()) {
      zxlogf(ERROR, "Failed to write: %d", result->result.err());
      return result->result.err();
    }

    buf += burst;
    command_size -= burst;
  }

  // There should be exactly one byte left.
  if (command_size != 1) {
    return ZX_ERR_BAD_STATE;
  }

  do {
    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
  } while (sts.sts_valid() == 0);
  if (sts.expect() != 1) {
    zxlogf(ERROR, "TPM should expect more data!");
    return ZX_ERR_BAD_STATE;
  }
  auto result =
      tpm_.Write(0, RegisterAddress::kTpmDataFifo, fidl::VectorView<uint8_t>::FromExternal(buf, 1));
  if (!result.ok()) {
    zxlogf(ERROR, "FIDL call failed!");
    return result.status();
  }
  if (result->result.is_err()) {
    zxlogf(ERROR, "Failed to write: %d", result->result.err());
    return result->result.err();
  }

  status = sts.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }

  if (sts.expect() == 1) {
    zxlogf(ERROR, "TPM expected more bytes than we wrote.");
    return ZX_ERR_INTERNAL;
  }

  status = StsReg().set_tpm_go(1).WriteTo(tpm_);
  if (status != ZX_OK) {
    return status;
  }

  status = sts.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }
  while (sts.data_avail() == 0) {
    // Wait for a response.
    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(500)));
  }

  // Read the response header.
  TpmResponseHeader response;
  status =
      ReadFromFifo(cpp20::span<uint8_t>(reinterpret_cast<uint8_t *>(&response), sizeof(response)));
  if (status != ZX_OK) {
    return status;
  }

  // If the response is just the response header, avoid an extra allocation.
  if (response.ResponseSize() == sizeof(response)) {
    cmd.handler(ZX_OK, &response);
    return ZX_OK;
  }

  // Otherwise, allocate a buffer that's large enough to fit the whole response.
  std::vector<uint8_t> data(response.ResponseSize());
  memcpy(data.data(), &response, sizeof(response));
  uint8_t *next = &data[sizeof(response)];
  size_t bytes = data.size() - sizeof(response);
  status = ReadFromFifo(cpp20::span<uint8_t>(next, bytes));
  if (status != ZX_OK) {
    return status;
  }

  cmd.handler(ZX_OK, reinterpret_cast<TpmResponseHeader *>(data.data()));
  return ZX_OK;
}

zx_status_t TpmDevice::ReadFromFifo(cpp20::span<uint8_t> data) {
  StsReg sts;
  zx_status_t status = sts.ReadFrom(tpm_);
  if (status != ZX_OK) {
    return status;
  }
  size_t read = 0;
  while (read < data.size_bytes() && sts.data_avail()) {
    size_t burst_count = std::min(static_cast<size_t>(sts.burst_count()), data.size_bytes() - read);
    if (burst_count != 0) {
      auto result = tpm_.Read(0, RegisterAddress::kTpmDataFifo, burst_count);
      if (!result.ok()) {
        zxlogf(ERROR, "FIDL call failed!");
        return result.status();
      }
      if (result->result.is_err()) {
        zxlogf(ERROR, "Failed to read: %d", result->result.err());
        return result->result.err();
      }
      auto &received = result->result.response().data;

      memcpy(&data.data()[read], received.data(), received.count());
      read += received.count();
    }

    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (read < data.size_bytes()) {
    return ZX_ERR_IO;
  }

  return status;
}

static const zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TpmDevice::Create;
  return ops;
}();
}  // namespace tpm

// clang-format off
ZIRCON_DRIVER(tpm, tpm::driver_ops, "zircon", "0.1");
