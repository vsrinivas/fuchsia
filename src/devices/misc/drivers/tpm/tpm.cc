// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Very basic TPM driver
 *
 * Assumptions:
 * - The system firmware is responsible for initializing the TPM and has
 *   already done so.
 */

#include "tpm.h"

#include <assert.h>
#include <endian.h>
#include <lib/driver-unit-test/utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/types.h>

#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <explicit-memory/bytes.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "i2c-cr50.h"
#include "tpm-commands.h"

// This is arbitrary, we just want to limit the size of the response buffer
// that we need to allocate.
#define MAX_RAND_BYTES 256

namespace tpm {

// implement tpm protocol:

zx_status_t Device::GetRandom(void* buf, uint16_t count, size_t* actual) {
  static_assert(MAX_RAND_BYTES <= UINT32_MAX, "");
  if (count > MAX_RAND_BYTES) {
    count = MAX_RAND_BYTES;
  }

  struct tpm_getrandom_cmd cmd;
  uint32_t resp_len = tpm_init_getrandom(&cmd, count);
  std::unique_ptr<uint8_t[]> resp_buf(new uint8_t[resp_len]);
  size_t actual_read;
  uint16_t bytes_returned;

  zx_status_t status =
      ExecuteCmd(0, (uint8_t*)&cmd, sizeof(cmd), resp_buf.get(), resp_len, &actual_read);
  if (status != ZX_OK) {
    return status;
  }

  auto resp = reinterpret_cast<tpm_getrandom_resp*>(resp_buf.get());
  if (actual_read < sizeof(*resp) || actual_read != betoh32(resp->hdr.total_len)) {
    return ZX_ERR_BAD_STATE;
  }
  bytes_returned = betoh16(resp->bytes_returned);
  if (actual_read != sizeof(*resp) + bytes_returned ||
      resp->hdr.tag != htobe16(TPM_ST_NO_SESSIONS) || bytes_returned > count ||
      resp->hdr.return_code != htobe32(TPM_SUCCESS)) {
    return ZX_ERR_BAD_STATE;
  }
  memcpy(buf, resp->bytes, bytes_returned);
  mandatory_memset(resp->bytes, 0, bytes_returned);
  *actual = bytes_returned;
  return ZX_OK;
}

zx_status_t Device::ShutdownLocked(uint16_t type) {
  struct tpm_shutdown_cmd cmd;
  uint32_t resp_len = tpm_init_shutdown(&cmd, type);
  struct tpm_shutdown_resp resp;
  size_t actual;

  zx_status_t status =
      ExecuteCmdLocked(0, (uint8_t*)&cmd, sizeof(cmd), (uint8_t*)&resp, resp_len, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual < sizeof(resp) || actual != betoh32(resp.hdr.total_len) ||
      resp.hdr.tag != htobe16(TPM_ST_NO_SESSIONS) || resp.hdr.return_code != htobe32(TPM_SUCCESS)) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t Device::Create(void* ctx, zx_device_t* parent, std::unique_ptr<Device>* out) {
  zx::handle irq;
  i2c_protocol_t i2c;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "tpm: could not get I2C protocol: %d", status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = i2c_get_interrupt(&i2c, 0, irq.reset_and_get_address());
  if (status == ZX_OK) {
    // irq contains garbage?
    zx_handle_t ignored __UNUSED = irq.release();
  }

  std::unique_ptr<I2cCr50Interface> i2c_iface;
  status = I2cCr50Interface::Create(parent, std::move(irq), &i2c_iface);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::make_unique<Device>(parent, std::move(i2c_iface));
  return ZX_OK;
}

zx_status_t Device::CreateAndBind(void* ctx, zx_device_t* parent) {
  std::unique_ptr<Device> device;
  zx_status_t status = Create(ctx, parent, &device);
  if (status != ZX_OK) {
    return status;
  }

  status = device->Bind();
  if (status == ZX_OK) {
    // DevMgr now owns this pointer, release it to avoid destroying the
    // object when device goes out of scope.
    __UNUSED auto* ptr = device.release();
  }
  return status;
}

zx_status_t Device::ExecuteCmd(Locality loc, const uint8_t* cmd, size_t len, uint8_t* resp,
                               size_t max_len, size_t* actual) {
  fbl::AutoLock guard(&lock_);
  return ExecuteCmdLocked(loc, cmd, len, resp, max_len, actual);
}

zx_status_t Device::ExecuteCmdLocked(Locality loc, const uint8_t* cmd, size_t len, uint8_t* resp,
                                     size_t max_len, size_t* actual) {
  zx_status_t status = SendCmdLocked(loc, cmd, len);
  if (status != ZX_OK) {
    return status;
  }
  return RecvRespLocked(loc, resp, max_len, actual);
}

void Device::DdkRelease() { delete this; }

void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  {
    fbl::AutoLock guard(&lock_);
    ReleaseLocalityLocked(0);
  }
  txn.Reply();
}

zx_status_t Device::Suspend(uint8_t requested_state, bool wakeup_enabled, uint8_t suspend_reason,
                            uint8_t* out_state) {
  // TODO(fxb/43205): Implement suspend hook, based on the requested
  // low power state and suspend reason. Also make this asynchronous
  fbl::AutoLock guard(&lock_);

  if (suspend_reason == DEVICE_SUSPEND_REASON_SUSPEND_RAM) {
    zx_status_t status = ShutdownLocked(TPM_SU_STATE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "tpm: Failed to save state: %d", status);
      *out_state = DEV_POWER_STATE_D0;
      return status;
    }
  }

  zx_status_t status = ReleaseLocalityLocked(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "tpm: Failed to release locality: %d", status);
    *out_state = DEV_POWER_STATE_D0;
    return status;
  }
  *out_state = requested_state;
  return status;
}

void Device::DdkSuspendNew(ddk::SuspendTxn txn) {
  uint8_t out_state;
  zx_status_t status =
      Device::Suspend(txn.requested_state(), txn.enable_wake(), txn.suspend_reason(), &out_state);
  txn.Reply(status, out_state);
}

zx_status_t Device::Bind() {
  zx_status_t status = DdkAdd("tpm", DEVICE_ADD_INVISIBLE);
  if (status != ZX_OK) {
    return status;
  }

  thrd_t thread;
  int ret = thrd_create_with_name(&thread, InitThread, this, "tpm:slow_bind");
  if (ret != thrd_success) {
    DdkRemoveDeprecated();
    return ZX_ERR_INTERNAL;
  }
  thrd_detach(thread);
  return ZX_OK;
}

zx_status_t Device::Init() {
  zx_status_t status = iface_->Validate();
  if (status != ZX_OK) {
    zxlogf(TRACE, "tpm: did not pass driver validation");
    return status;
  }

  {
    fbl::AutoLock guard(&lock_);

    // tpm_request_use will fail if we're not at least 30ms past _TPM_INIT.
    // The system firmware performs the init, so it's safe to assume that
    // is 30 ms past.  If we're on systems where we need to do init,
    // we need to wait up to 30ms for the TPM_ACCESS register to be valid.
    status = RequestLocalityLocked(0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "tpm: Failed to request use: %d", status);
      return status;
    }

    status = WaitForLocalityLocked(0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "tpm: Waiting for locality failed: %d", status);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Device::InitThread() {
  uint8_t buf[32] = {0};
  size_t bytes_read;

  auto cleanup = fbl::MakeAutoCall([&] { DdkRemoveDeprecated(); });

  zx_status_t status = Init();
  if (status != ZX_OK) {
    return status;
  }

  DdkMakeVisible();

  // Make a best-effort attempt to give the kernel some more entropy
  // TODO(security): Perform a more recurring seeding
  status = GetRandom(buf, static_cast<uint16_t>(sizeof(buf)), &bytes_read);
  if (status == ZX_OK) {
    zx_cprng_add_entropy(buf, bytes_read);
    mandatory_memset(buf, 0, sizeof(buf));
  } else {
    zxlogf(ERROR, "tpm: Failed to add entropy to kernel CPRNG");
  }

  cleanup.cancel();
  return ZX_OK;
}

bool Device::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("TpmTests", parent, channel);
}

Device::Device(zx_device_t* parent, std::unique_ptr<HardwareInterface> iface)
    : DeviceType(parent), iface_(std::move(iface)) {
  ddk_proto_id_ = ZX_PROTOCOL_TPM;
}

Device::~Device() {}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = tpm::Device::CreateAndBind;
  ops.run_unit_tests = tpm::Device::RunUnitTests;
  return ops;
}();

}  // namespace tpm

// clang-format off
ZIRCON_DRIVER_BEGIN(tpm, tpm::driver_ops, "zircon", "0.1", 3)
    // Handle I2C
    // TODO(teisenbe): Make this less hacky when we have a proper I2C protocol
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_ABORT_IF(NE, BIND_PCI_DID, 0x9d61),
    BI_MATCH_IF(EQ, BIND_TOPO_I2C, BIND_TOPO_I2C_PACK(0x0050)),
ZIRCON_DRIVER_END(tpm)
    // clang-format on
