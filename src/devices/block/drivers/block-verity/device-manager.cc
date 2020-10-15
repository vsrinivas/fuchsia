// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/block-verity/device-manager.h"

#include <fuchsia/hardware/block/verified/llcpp/fidl.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>

#include "src/devices/block/drivers/block-verity/config.h"
#include "src/devices/block/drivers/block-verity/device.h"
#include "src/devices/block/drivers/block-verity/superblock-verifier.h"
#include "src/devices/block/drivers/block-verity/verified-device.h"

namespace block_verity {
namespace {

void SealCompletedCallback(void* cookie, zx_status_t status, const uint8_t* seal_buf,
                           size_t seal_len) {
  auto device_manager = static_cast<DeviceManager*>(cookie);
  device_manager->OnSealCompleted(status, seal_buf, seal_len);
}

void SuperblockVerificationCallback(void* cookie, zx_status_t status,
                                    const Superblock* superblock) {
  auto device_manager = static_cast<DeviceManager*>(cookie);
  device_manager->OnSuperblockVerificationCompleted(status, superblock);
}

}  // namespace

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

  // devmgr is now in charge of the memory for `manager`.
  __UNUSED auto* owned_by_devmgr_now = manager.release();

  return ZX_OK;
}

zx_status_t DeviceManager::Bind() {
  zx_status_t rc;
  fbl::AutoLock lock(&mtx_);

  if ((rc = DdkAdd("verity")) != ZX_OK) {
    zxlogf(ERROR, "failed to add verity device: %s", zx_status_get_string(rc));
    state_ = kRemoved;
    return rc;
  }

  state_ = kClosed;
  return ZX_OK;
}

void DeviceManager::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  // Mark the device as getting-removed, so we refuse all other FIDL calls.
  state_ = kRemoved;

  // Signal that unbind is completed; child devices can be removed
  txn.Reply();
}

void DeviceManager::DdkRelease() { delete this; }

zx_status_t DeviceManager::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::block::verified::DeviceManager::Dispatch(this, msg, &transaction);
  return ZX_ERR_ASYNC;
}

void DeviceManager::DdkChildPreRelease(void* child_ctx) {
  fbl::AutoLock lock(&mtx_);

  switch (state_) {
    case kAuthoring:
    case kVerifiedRead:
      // The underlying device disappeared unexpectedly.  Drop our reference to
      // it, and mark our state as kError so we don't wind up doing anything
      // dangerous.
      mutable_child_ = std::nullopt;
      verified_child_ = std::nullopt;
      state_ = kError;
      break;
    case kClosing:
      ZX_ASSERT(mutable_child_.has_value() || verified_child_.has_value());
      if (mutable_child_.has_value()) {
        ZX_ASSERT(child_ctx == *mutable_child_);
      }
      if (verified_child_.has_value()) {
        ZX_ASSERT(child_ctx == *verified_child_);
      }
      ZX_ASSERT(close_completer_.has_value());
      mutable_child_ = std::nullopt;
      verified_child_ = std::nullopt;
      close_completer_->ReplySuccess();
      close_completer_ = std::nullopt;
      state_ = kClosed;
      break;
    case kClosingForSeal: {
      state_ = kSealing;
      mutable_child_ = std::nullopt;
      // Now that the mutable device is unbound and about to release, we can
      // start generating integrity data.
      DeviceInfo info = DeviceInfo::CreateFromDevice(parent());
      if (!info.IsValid()) {
        zxlogf(ERROR, "failed to get valid device info");
        seal_completer_->ReplyError(ZX_ERR_BAD_STATE);
        seal_completer_ = std::nullopt;
        state_ = kError;
        return;
      }
      sealer_ = std::make_unique<DriverSealer>(std::move(info));
      // The sealer will recompute and write out all verified block data, update
      // the superblock, issue a flush, and then return the hash of the
      // superblock.
      zx_status_t result = sealer_->StartSealing(this, SealCompletedCallback);
      if (result != ZX_OK) {
        zxlogf(ERROR, "sealer failed to start: %d", result);
        state_ = kError;
      }
      break;
    }
    case kBinding:
    case kClosed:
    case kSealing:
    case kVerifiedReadCheck:
    case kError:
    case kUnbinding:
    case kRemoved:
      zxlogf(ERROR, "Got unexpected child prerelease notification while in state %d", state_);
      break;
  }
}

void DeviceManager::OpenForWrite(llcpp::fuchsia::hardware::block::verified::Config config,
                                 OpenForWriteCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  if (state_ != kClosed) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  block_info_t blk;
  size_t op_size;
  ddk::BlockProtocolClient block_protocol_client(parent());
  block_protocol_client.Query(&blk, &op_size);

  // Check args
  zx_status_t rc = CheckConfig(config, blk);
  if (rc != ZX_OK) {
    zxlogf(WARNING, "Refusing OpenForWrite: invalid config");
    async_completer.ReplyError(rc);
    return;
  }

  // If we make it to here, all arguments have been validated.
  // Go ahead and create the mutable child device.

  fbl::AllocChecker ac;
  DeviceInfo info = DeviceInfo::CreateFromDevice(parent());
  if (!info.IsValid()) {
    zxlogf(ERROR, "failed to get valid device info");
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  auto device = fbl::make_unique_checked<block_verity::Device>(&ac, zxdev(), std::move(info));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes", sizeof(Device));
    async_completer.ReplyError(ZX_ERR_NO_MEMORY);
    return;
  }

  if ((rc = device->DdkAdd("mutable")) != ZX_OK) {
    zxlogf(ERROR, "failed to add mutable device: %s", zx_status_get_string(rc));
    async_completer.ReplyError(rc);
    return;
  }
  zxlogf(INFO, "added block-verity mutable child");

  // devmgr now owns the memory for `device`, but it'll send us a
  // ChildPreRelease hook notification before it destroys it.
  mutable_child_ = device.release();

  state_ = kAuthoring;
  async_completer.ReplySuccess();
}

void DeviceManager::CloseAndGenerateSeal(CloseAndGenerateSealCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  llcpp::fuchsia::hardware::block::verified::Seal seal;
  if (state_ != kAuthoring) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  // Unbind the appropriate child device.  We'll wait for the prerelease hook to be
  // called to ensure that new reads and writes have quiesced before we start sealing.
  state_ = kClosingForSeal;
  (*mutable_child_)->DdkAsyncRemove();

  // Stash the completer somewhere so we can signal it when we've finished
  // generating the seal.
  seal_completer_ = std::move(async_completer);
}

void DeviceManager::OnSealCompleted(zx_status_t status, const uint8_t* seal_buf, size_t seal_len) {
  fbl::AutoLock lock(&mtx_);
  ZX_ASSERT(state_ == kSealing);
  ZX_ASSERT(seal_completer_.has_value());
  ZX_ASSERT(seal_len == 32);

  if (status == ZX_OK) {
    // Assemble the result struct and reply with success
    llcpp::fuchsia::hardware::block::verified::Sha256Seal sha256;
    memcpy(sha256.superblock_hash.begin(), seal_buf, seal_len);

    fidl::aligned<llcpp::fuchsia::hardware::block::verified::Sha256Seal> aligned =
        std::move(sha256);
    seal_completer_->ReplySuccess(
        llcpp::fuchsia::hardware::block::verified::Seal::WithSha256(fidl::unowned_ptr(&aligned)));
  } else {
    zxlogf(WARNING, "Sealer returned failure: %s", zx_status_get_string(status));
    seal_completer_->ReplyError(status);
  }

  // Clean up.
  state_ = kClosed;
  sealer_.reset();
  seal_completer_ = std::nullopt;
}

void DeviceManager::OnSuperblockVerificationCompleted(zx_status_t status,
                                                      const Superblock* superblock) {
  fbl::AutoLock lock(&mtx_);
  ZX_ASSERT(state_ == kVerifiedReadCheck);
  ZX_ASSERT(open_for_verified_read_completer_.has_value());

  if (status != ZX_OK) {
    zxlogf(WARNING, "Superblock verifier returned failure: %s", zx_status_get_string(status));
    CompleteOpenForVerifiedRead(status);
    return;
  }

  // Great, looks good.  Let's set up that VerifiedDevice with the superblock
  // root hash.
  fbl::AllocChecker ac;
  DeviceInfo info = DeviceInfo::CreateFromDevice(parent());
  if (!info.IsValid()) {
    zxlogf(ERROR, "failed to get valid device info");
    CompleteOpenForVerifiedRead(ZX_ERR_BAD_STATE);
    return;
  }

  // Extract integrity root hash from superblock.
  std::array<uint8_t, kHashOutputSize> integrity_root_hash;
  memcpy(integrity_root_hash.data(), superblock->integrity_root_hash, kHashOutputSize);

  // Allocate device.
  auto device = fbl::make_unique_checked<block_verity::VerifiedDevice>(
      &ac, zxdev(), std::move(info), integrity_root_hash);
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes", sizeof(Device));
    CompleteOpenForVerifiedRead(ZX_ERR_NO_MEMORY);
    return;
  }

  zx_status_t rc;
  if ((rc = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "failed to prepare verified device: %s", zx_status_get_string(rc));
    CompleteOpenForVerifiedRead(rc);
    return;
  }

  if ((rc = device->DdkAdd("verified")) != ZX_OK) {
    zxlogf(ERROR, "failed to add verified device: %s", zx_status_get_string(rc));
    CompleteOpenForVerifiedRead(rc);
    return;
  }
  zxlogf(INFO, "added block-verity verified child");

  // devmgr now owns the memory for `device`, but it'll send us a
  // ChildPreRelease hook notification before it destroys it.
  verified_child_ = device.release();
  CompleteOpenForVerifiedRead(ZX_OK);
}

void DeviceManager::CompleteOpenForVerifiedRead(zx_status_t status) {
  if (status != ZX_OK) {
    open_for_verified_read_completer_->ReplyError(status);
    state_ = kClosed;
  } else {
    open_for_verified_read_completer_->ReplySuccess();
    state_ = kVerifiedRead;
  }

  open_for_verified_read_completer_ = std::nullopt;
  superblock_verifier_.reset();
}

void DeviceManager::OpenForVerifiedRead(llcpp::fuchsia::hardware::block::verified::Config config,
                                        llcpp::fuchsia::hardware::block::verified::Seal seal,
                                        OpenForVerifiedReadCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  if (state_ != kClosed) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  block_info_t blk;
  size_t op_size;
  ddk::BlockProtocolClient block_protocol_client(parent());
  block_protocol_client.Query(&blk, &op_size);

  // Check args.
  zx_status_t rc = CheckConfig(config, blk);
  if (rc != ZX_OK) {
    zxlogf(WARNING, "Refusing OpenForVerifiedRead: invalid config");
    async_completer.ReplyError(rc);
    return;
  }

  // Stash the completer somewhere so we can signal it when we've finished
  // verifying the superblock.
  open_for_verified_read_completer_ = std::move(async_completer);

  // Load superblock.  Check seal.  Check config matches seal.
  DeviceInfo info = DeviceInfo::CreateFromDevice(parent());
  std::array<uint8_t, kHashOutputSize> expected_hash;
  memcpy(expected_hash.data(), seal.sha256().superblock_hash.data(), kHashOutputSize);
  superblock_verifier_ = std::make_unique<SuperblockVerifier>(std::move(info), expected_hash);
  state_ = kVerifiedReadCheck;
  superblock_verifier_->StartVerifying(this, SuperblockVerificationCallback);
}

void DeviceManager::Close(CloseCompleter::Sync& completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  if (state_ != kAuthoring && state_ != kVerifiedRead) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  // Request the appropriate child be removed.
  state_ = kClosing;
  if (mutable_child_.has_value()) {
    (*mutable_child_)->DdkAsyncRemove();
  }
  if (verified_child_.has_value()) {
    (*verified_child_)->DdkAsyncRemove();
  }

  // Stash the completer somewhere so we can signal it when we get the
  // DdkChildPreRelease hook call.
  close_completer_ = std::move(async_completer);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = DeviceManager::Create;
  return ops;
}();

}  // namespace block_verity

// clang-format off
ZIRCON_DRIVER_BEGIN(block_verity, block_verity::driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(block_verity)
    // clang-format on
