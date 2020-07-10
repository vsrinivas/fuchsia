// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-manager.h"

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

#include "device.h"

namespace block_verity {

zx_status_t DeviceManager::Create(void* ctx, zx_device_t* parent) {
  zx_status_t rc;
  fbl::AllocChecker ac;

  auto manager = fbl::make_unique_checked<DeviceManager>(&ac, parent);
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes\n", sizeof(DeviceManager));
    return ZX_ERR_NO_MEMORY;
  }

  if ((rc = manager->Bind()) != ZX_OK) {
    zxlogf(ERROR, "failed to bind: %s\n", zx_status_get_string(rc));
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
    zxlogf(ERROR, "failed to add device: %s\n", zx_status_get_string(rc));
    state_ = kRemoved;
    return rc;
  }

  state_ = kClosed;
  return ZX_OK;
}

void DeviceManager::DdkUnbindNew(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  // Mark the device as getting-removed, so we refuse all other FIDL calls.
  state_ = kRemoved;

  zxlogf(INFO, "DdkUnbindNew called\n");

  // Signal that unbind is completed; child devices can be removed
  txn.Reply();
}

void DeviceManager::DdkRelease() {
  zxlogf(INFO, "DdkRelease called\n");
  delete this;
}

zx_status_t DeviceManager::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::block::verified::DeviceManager::Dispatch(this, msg, &transaction);
  return ZX_ERR_ASYNC;
}

void DeviceManager::DdkChildPreRelease(void* child_ctx) {
  fbl::AutoLock lock(&mtx_);
  zxlogf(INFO, "got notified of child prerelease %p\n", child_ctx);

  switch (state_) {
    case kAuthoring:
    case kVerifiedRead:
      // The underlying device disappeared unexpectedly.  Drop our reference to
      // it, and mark our state as kError so we don't wind up doing anything
      // dangerous.
      child_ = std::nullopt;
      state_ = kError;
      break;
    case kClosing:
      ZX_ASSERT(child_ctx == *child_);
      ZX_ASSERT(close_completer_.has_value());
      child_ = std::nullopt;
      close_completer_->ReplySuccess();
      close_completer_ = std::nullopt;
      state_ = kClosed;
      break;
    case kClosingForSeal:
      state_ = kSealing;
      // TODO: start generating integrity data
      break;
    case kBinding:
    case kClosed:
    case kSealing:
    case kError:
    case kUnbinding:
    case kRemoved:
      zxlogf(ERROR, "Got unexpected child prerelease notification while in state %d", state_);
      break;
  }
}

void DeviceManager::OpenForWrite(::llcpp::fuchsia::hardware::block::verified::Config config,
                                 OpenForWriteCompleter::Sync completer) {
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

  // Check that the config specifies a supported hash function
  if (!config.has_hash_function()) {
    zxlogf(WARNING, "Config did not specify a hash function");
    async_completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  switch (config.hash_function()) {
    case ::llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256:
      break;
    default:
      zxlogf(WARNING, "Unknown hash function enum value %hhu\n", config.hash_function());
      async_completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
  }
  zxlogf(INFO, "hash function was valid");

  // Check that the config specifies a supported block size, and that the block
  // size matches that of the underlying block device
  if (!config.has_block_size()) {
    zxlogf(WARNING, "Config did not specify a block size");
    async_completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  switch (config.block_size()) {
    case ::llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096:
      // Verify that the block size from the device matches the value requested.
      if (blk.block_size != 4096) {
        zxlogf(WARNING, "Config specified block size 4096 but underlying block size is %d\n",
               blk.block_size);
        async_completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      break;
    default:
      zxlogf(WARNING, "Unknown block size enum value %hhu\n", config.block_size());
      async_completer.ReplyError(ZX_ERR_INVALID_ARGS);
      return;
  }
  zxlogf(INFO, "block size was valid");

  // If we make it to here, all arguments have been validated.
  // Go ahead and create the mutable child device.

  fbl::AllocChecker ac;
  DeviceInfo info(parent());
  if (!info.IsValid()) {
    zxlogf(ERROR, "failed to get valid device info");
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  zxlogf(INFO, "device info was valid");

  auto device = fbl::make_unique_checked<block_verity::Device>(&ac, zxdev(), std::move(info));
  if (!ac.check()) {
    zxlogf(ERROR, "failed to allocate %zu bytes\n", sizeof(Device));
    async_completer.ReplyError(ZX_ERR_NO_MEMORY);
    return;
  }
  zxlogf(INFO, "allocated device");

  zx_status_t rc;
  if ((rc = device->DdkAdd("mutable")) != ZX_OK) {
    zxlogf(ERROR, "failed to add device: %s", zx_status_get_string(rc));
    async_completer.ReplyError(rc);
    return;
  }

  zxlogf(INFO, "added device at mutable");

  // devmgr now owns the memory for `device`, but it'll send us a
  // ChildPreRelease hook notification before it destroys it.
  child_ = device.release();

  state_ = kAuthoring;
  async_completer.ReplySuccess();
}

void DeviceManager::CloseAndGenerateSeal(CloseAndGenerateSealCompleter::Sync completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  ::llcpp::fuchsia::hardware::block::verified::Seal seal;
  if (state_ != kAuthoring) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  // TODO: implement
  //
  // Unbind the mutable child device
  // wait for that child to be removed
  // recompute and write out all verified block data
  // flush
  // return root hash
  async_completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DeviceManager::OpenForVerifiedRead(::llcpp::fuchsia::hardware::block::verified::Config config,
                                        ::llcpp::fuchsia::hardware::block::verified::Seal seal,
                                        OpenForVerifiedReadCompleter::Sync completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  if (state_ != kClosed) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  // TODO: create the verified device
  async_completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DeviceManager::Close(CloseCompleter::Sync completer) {
  fbl::AutoLock lock(&mtx_);
  auto async_completer = completer.ToAsync();
  if (state_ != kAuthoring && state_ != kVerifiedRead) {
    async_completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  // Request the child be removed.
  state_ = kClosing;
  (*child_)->DdkAsyncRemove();

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
