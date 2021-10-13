// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/zxcrypt/device-manager.h"

#include <fidl/fuchsia.hardware.block.encrypted/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>

#include "src/devices/block/drivers/zxcrypt/device-info.h"
#include "src/devices/block/drivers/zxcrypt/device.h"
#include "src/devices/block/drivers/zxcrypt/zxcrypt-bind.h"
#include "src/security/fcrypto/secret.h"
#include "src/security/zxcrypt/ddk-volume.h"
#include "src/security/zxcrypt/volume.h"

namespace zxcrypt {

zx_status_t DeviceManager::Create(void* ctx, zx_device_t* parent) {
  zx_status_t rc;
  fbl::AllocChecker ac;

  auto manager = fbl::make_unique_checked<DeviceManager>(&ac, parent);
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes", sizeof(DeviceManager));
    return ZX_ERR_NO_MEMORY;
  }

  if ((rc = manager->Bind()) != ZX_OK) {
    zxlogf(ERROR, "failed to bind: %s", zx_status_get_string(rc));
    return rc;
  }

  // devmgr is now in charge of the memory for |manager|.
  __UNUSED auto* owned_by_devmgr_now = manager.release();

  return ZX_OK;
}

zx_status_t DeviceManager::Bind() {
  zx_status_t rc;
  fbl::AutoLock lock(&mtx_);

  if ((rc = DdkAdd(ddk::DeviceAddArgs("zxcrypt").set_flags(DEVICE_ADD_NON_BINDABLE))) != ZX_OK) {
    zxlogf(ERROR, "failed to add device: %s", zx_status_get_string(rc));
    state_ = kRemoved;
    return rc;
  }

  state_ = kSealed;
  return ZX_OK;
}

void DeviceManager::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  ZX_ASSERT(state_ == kSealed || state_ == kUnsealed || state_ == kShredded);
  state_ = kRemoved;
  txn.Reply();
}

void DeviceManager::DdkRelease() { delete this; }

void DeviceManager::Format(FormatRequestView request, FormatCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  if (state_ != kSealed) {
    zxlogf(ERROR, "can't format zxcrypt, state=%d", state_);
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }
  completer.Reply(FormatLocked(request->key.data(), request->key.count(), 0));
}

void DeviceManager::Unseal(UnsealRequestView request, UnsealCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  if (state_ != kSealed) {
    zxlogf(ERROR, "can't unseal zxcrypt, state=%d", state_);
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }
  completer.Reply(UnsealLocked(request->key.data(), request->key.count(), request->slot));
}

void DeviceManager::Seal(SealRequestView request, SealCompleter::Sync& completer) {
  zx_status_t rc;
  fbl::AutoLock lock(&mtx_);

  if (state_ != kUnsealed && state_ != kShredded) {
    zxlogf(ERROR, "can't seal zxcrypt, state=%d", state_);
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }
  if ((rc = device_rebind(zxdev())) != ZX_OK) {
    zxlogf(ERROR, "failed to rebind zxcrypt: %s", zx_status_get_string(rc));
    completer.Reply(rc);
    return;
  }

  state_ = kSealed;
  completer.Reply(ZX_OK);
}

void DeviceManager::Shred(ShredRequestView request, ShredCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);

  // We want to shred the underlying volume, but if we have an unsealed device,
  // we don't mind letting it keep working for now.  Other parts of the system
  // would rather we shut down gracefully than immediately stop permitting reads
  // or acking writes.  So we instantiate a new DdkVolume here, quietly shred
  // it, and let child devices carry on as if nothing happened.
  std::unique_ptr<DdkVolume> volume_to_shred;
  zx_status_t rc;
  rc = DdkVolume::OpenOpaque(parent(), &volume_to_shred);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "failed to open volume to shred: %s", zx_status_get_string(rc));
    completer.Reply(rc);
    return;
  }

  rc = volume_to_shred->Shred();
  if (rc != ZX_OK) {
    zxlogf(ERROR, "failed to shred volume: %s", zx_status_get_string(rc));
    completer.Reply(rc);
    return;
  }

  state_ = kShredded;
  completer.Reply(ZX_OK);
}

zx_status_t DeviceManager::FormatLocked(const uint8_t* ikm, size_t ikm_len, key_slot_t slot) {
  zx_status_t rc;

  crypto::Secret key;
  uint8_t* buf;
  if ((rc = key.Allocate(ikm_len, &buf)) != ZX_OK) {
    zxlogf(ERROR, "failed to allocate %zu-byte key: %s", ikm_len, zx_status_get_string(rc));
    return rc;
  }
  memcpy(buf, ikm, key.len());
  std::unique_ptr<DdkVolume> volume;
  if ((rc = DdkVolume::OpenOpaque(parent(), &volume)) != ZX_OK) {
    zxlogf(ERROR, "failed to open volume: %s", zx_status_get_string(rc));
    return rc;
  }

  if ((rc = volume->Format(key, slot)) != ZX_OK) {
    zxlogf(ERROR, "failed to format: %s", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t DeviceManager::UnsealLocked(const uint8_t* ikm, size_t ikm_len, key_slot_t slot) {
  zx_status_t rc;

  // Unseal the zxcrypt volume.
  crypto::Secret key;
  uint8_t* buf;
  if ((rc = key.Allocate(ikm_len, &buf)) != ZX_OK) {
    zxlogf(ERROR, "failed to allocate %zu-byte key: %s", ikm_len, zx_status_get_string(rc));
    return rc;
  }
  memcpy(buf, ikm, key.len());
  std::unique_ptr<DdkVolume> volume;
  if ((rc = DdkVolume::Unlock(parent(), key, slot, &volume)) != ZX_OK) {
    zxlogf(ERROR, "failed to unseal volume: %s", zx_status_get_string(rc));
    return rc;
  }

  // Get the parent device's configuration details.
  DeviceInfo info(parent(), *volume);
  if (!info.IsValid()) {
    zxlogf(ERROR, "failed to get valid device info");
    return ZX_ERR_BAD_STATE;
  }
  // Reserve space for shadow I/O transactions
  if ((rc = info.Reserve(Volume::kBufferSize)) != ZX_OK) {
    zxlogf(ERROR, "failed to reserve buffer for I/O: %s", zx_status_get_string(rc));
    return rc;
  }

  // Create the unsealed device
  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<zxcrypt::Device>(&ac, zxdev(), std::move(info));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes", sizeof(zxcrypt::Device));
    return ZX_ERR_NO_MEMORY;
  }
  if ((rc = device->Init(*volume)) != ZX_OK) {
    zxlogf(ERROR, "failed to initialize device: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = device->DdkAdd("unsealed")) != ZX_OK) {
    zxlogf(ERROR, "failed to add device: %s", zx_status_get_string(rc));
    return rc;
  }

  // devmgr is now in charge of the memory for |device|
  __UNUSED auto owned_by_devmgr_now = device.release();
  state_ = kUnsealed;
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = DeviceManager::Create;
  return ops;
}();

}  // namespace zxcrypt

ZIRCON_DRIVER(zxcrypt, zxcrypt::driver_ops, "zircon", "0.1");
