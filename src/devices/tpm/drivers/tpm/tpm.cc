// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tpm/drivers/tpm/tpm.h"

#include <fuchsia/hardware/tpmimpl/cpp/banjo.h>
#include <fuchsia/hardware/tpmimpl/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/devices/tpm/drivers/tpm/commands.h"
#include "src/devices/tpm/drivers/tpm/registers.h"
#include "src/devices/tpm/drivers/tpm/tpm_bind.h"

namespace tpm {
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
  zx_status_t status = device->DdkAdd(ddk::DeviceAddArgs("tpm"));
  if (status == ZX_OK) {
    __UNUSED auto unused = device.release();
  }
  return status;
}

void TpmDevice::DdkInit(ddk::InitTxn txn) {
  zx_status_t status = ZX_OK;
  auto replier = fit::defer([&status, &txn]() { txn.Reply(status); });
  StsReg sts;
  if ((status = sts.ReadFrom(tpm_)) != ZX_OK) {
    return;
  }
  if (sts.tpm_family() != TpmFamily::kTpmFamily20) {
    zxlogf(ERROR, "unsupported TPM family, expected 2.0");

    status = ZX_ERR_NOT_SUPPORTED;
    return;
  }
}

void TpmDevice::DdkSuspend(ddk::SuspendTxn txn) {
  std::optional<TpmShutdownCmd> cmd;

  switch (txn.suspend_reason()) {
    case DEVICE_SUSPEND_REASON_REBOOT:
    case DEVICE_SUSPEND_REASON_REBOOT_BOOTLOADER:
    case DEVICE_SUSPEND_REASON_REBOOT_RECOVERY:
    case DEVICE_SUSPEND_REASON_POWEROFF:
      cmd = TpmShutdownCmd(TPM_SU_CLEAR);
      break;
    case DEVICE_SUSPEND_REASON_SUSPEND_RAM:
      cmd = TpmShutdownCmd(TPM_SU_STATE);
      break;
    default:
      zxlogf(WARNING, "Unknown suspend state %d", txn.requested_state());
      txn.Reply(ZX_OK, DEV_POWER_STATE_D0);
      return;
  }

  if (cmd.has_value()) {
    // TODO(fxbug.dev/81433): move this onto a command-processing thread.
    zx_status_t result = DoCommand(&cmd.value().hdr);
    if (result != ZX_OK) {
      zxlogf(ERROR, "Error sending TPM shutdown command: %s", zx_status_get_string(result));
      txn.Reply(result, DEV_POWER_STATE_D0);
      return;
    }
  }
  txn.Reply(ZX_OK, txn.requested_state());
}

zx_status_t TpmDevice::DoCommand(TpmCmdHeader *cmd) {
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

  size_t command_size = betoh32(cmd->command_size);
  uint8_t *buf = reinterpret_cast<uint8_t *>(cmd);
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

  while (sts.data_avail()) {
    // TODO(fxbug.dev/76095): actually return the result of the command.
    size_t burst_count = sts.burst_count();
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
    }

    status = sts.ReadFrom(tpm_);
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
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
