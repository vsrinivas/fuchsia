// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/zxcrypt/worker.h"

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/trace/event.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/port.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <utility>

#include "src/devices/block/drivers/zxcrypt/debug.h"
#include "src/devices/block/drivers/zxcrypt/device.h"
#include "src/devices/block/drivers/zxcrypt/extra.h"
#include "src/devices/block/drivers/zxcrypt/queue.h"
#include "src/security/fcrypto/cipher.h"
#include "src/security/zxcrypt/ddk-volume.h"
#include "src/security/zxcrypt/volume.h"

namespace zxcrypt {

Worker::Worker() : device_(nullptr), started_(false) { LOG_ENTRY(); }

Worker::~Worker() {
  LOG_ENTRY();
  ZX_DEBUG_ASSERT(!started_.load());
}

zx_status_t Worker::Start(Device* device, const DdkVolume& volume, Queue<block_op_t*>& queue) {
  LOG_ENTRY_ARGS("device=%p, volume=%p, queue=%p", device, &volume, &queue);
  zx_status_t rc;

  if (!device) {
    zxlogf(ERROR, "bad parameters: device=%p", device);
    return ZX_ERR_INVALID_ARGS;
  }
  device_ = device;

  if ((rc = volume.Bind(crypto::Cipher::kEncrypt, &encrypt_)) != ZX_OK ||
      (rc = volume.Bind(crypto::Cipher::kDecrypt, &decrypt_)) != ZX_OK) {
    zxlogf(ERROR, "failed to bind ciphers: %s", zx_status_get_string(rc));
    return rc;
  }

  queue_ = &queue;

  if (thrd_create_with_name(&thrd_, WorkerRun, this, "zxcrypt_worker") != thrd_success) {
    zxlogf(ERROR, "failed to start thread");
    return ZX_ERR_INTERNAL;
  }

  started_.store(true);
  return ZX_OK;
}

zx_status_t Worker::Run() {
  LOG_ENTRY();
  ZX_DEBUG_ASSERT(device_);

  for (;;) {
    block_op_t* block;
    if (auto block_or = queue_->Pop()) {
      block = *block_or;
    } else {
      zxlogf(DEBUG, "worker %p stopping.", this);
      return ZX_OK;
    }

    TRACE_DURATION("zxcrypt", "zxcrypt::Worker::Dispatch");

    // Dispatch block request
    switch (block->command & BLOCK_OP_MASK) {
      case BLOCK_OP_WRITE:
        device_->BlockForward(block, EncryptWrite(block));
        break;

      case BLOCK_OP_READ:
        device_->BlockComplete(block, DecryptRead(block));
        break;

      default:
        device_->BlockComplete(block, ZX_ERR_NOT_SUPPORTED);
    }
  }
}

zx_status_t Worker::EncryptWrite(block_op_t* block) {
  LOG_ENTRY_ARGS("block=%p", block);
  zx_status_t rc;

  // Convert blocks to bytes
  extra_op_t* extra = BlockToExtra(block, device_->op_size());
  uint32_t length;
  uint64_t offset_dev, offset_vmo;
  if (mul_overflow(block->rw.length, device_->block_size(), &length) ||
      mul_overflow(block->rw.offset_dev, device_->block_size(), &offset_dev) ||
      mul_overflow(extra->offset_vmo, device_->block_size(), &offset_vmo)) {
    zxlogf(ERROR, "overflow; length=%" PRIu32 "; offset_dev=%" PRIu64 "; offset_vmo=%" PRIu64 "",
           block->rw.length, block->rw.offset_dev, extra->offset_vmo);
    return ZX_ERR_OUT_OF_RANGE;
  }

  TRACE_DURATION("zxcrypt", "zxcrypt::Worker::EncryptWrite", "len", length);

  // Copy and encrypt the plaintext
  if ((rc = zx_vmo_read(extra->vmo, extra->data, offset_vmo, length)) != ZX_OK) {
    zxlogf(ERROR, "zx_vmo_read() failed: %s", zx_status_get_string(rc));
    return rc;
  }
  if ((rc = encrypt_.Encrypt(extra->data, offset_dev, length, extra->data) != ZX_OK)) {
    zxlogf(ERROR, "failed to encrypt: %s", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t Worker::DecryptRead(block_op_t* block) {
  LOG_ENTRY_ARGS("block=%p", block);
  zx_status_t rc;

  // Convert blocks to bytes
  uint32_t length;
  uint64_t offset_dev, offset_vmo;
  uint64_t mapping_offset = 0;
  if (mul_overflow(block->rw.length, device_->block_size(), &length) ||
      mul_overflow(block->rw.offset_dev, device_->block_size(), &offset_dev) ||
      mul_overflow(block->rw.offset_vmo, device_->block_size(), &offset_vmo)) {
    zxlogf(ERROR, "overflow; length=%" PRIu32 "; offset_dev=%" PRIu64 "; offset_vmo=%" PRIu64 "",
           block->rw.length, block->rw.offset_dev, block->rw.offset_vmo);
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint32_t aligned_length = length;

  TRACE_DURATION("zxcrypt", "zxcrypt::Worker::DecryptRead", "len", length);

  const size_t kPageSize = zx_system_get_page_size();

  if (ZX_ROUNDDOWN(offset_vmo, kPageSize) != offset_vmo) {
    // Ensure the range inside the VMO we map is page aligned so that requests smaller than a page
    // still work.
    mapping_offset = offset_vmo - ZX_ROUNDDOWN(offset_vmo, kPageSize);
    offset_vmo = ZX_ROUNDDOWN(offset_vmo, kPageSize);
    aligned_length += mapping_offset;
  }

  // Map the ciphertext
  zx_handle_t root = zx_vmar_root_self();
  uintptr_t address;
  constexpr uint32_t flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  if ((rc = zx_vmar_map(root, flags, 0, block->rw.vmo, offset_vmo, aligned_length, &address)) !=
      ZX_OK) {
    zxlogf(ERROR, "zx::vmar::root_self()->map() failed: %s", zx_status_get_string(rc));
    return rc;
  }
  auto cleanup = fit::defer(
      [root, address, aligned_length]() { zx_vmar_unmap(root, address, aligned_length); });

  // Decrypt in place
  uint8_t* data = reinterpret_cast<uint8_t*>(address + mapping_offset);
  if ((rc = decrypt_.Decrypt(data, offset_dev, length, data)) != ZX_OK) {
    zxlogf(ERROR, "failed to decrypt: %s", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

zx_status_t Worker::Stop() {
  LOG_ENTRY();
  zx_status_t rc;

  // Only join once per call to |Start|.
  if (!started_.exchange(false)) {
    return ZX_OK;
  }
  thrd_join(thrd_, &rc);

  if (rc != ZX_OK) {
    zxlogf(WARNING, "worker exited with error: %s", zx_status_get_string(rc));
    return rc;
  }

  return ZX_OK;
}

}  // namespace zxcrypt
