// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/zxcrypt/worker.h"

#include <inttypes.h>
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

#include <crypto/cipher.h>
#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <zxcrypt/ddk-volume.h>
#include <zxcrypt/volume.h>

#include "src/devices/block/drivers/zxcrypt/debug.h"
#include "src/devices/block/drivers/zxcrypt/device.h"
#include "src/devices/block/drivers/zxcrypt/extra.h"

namespace zxcrypt {

Worker::Worker() : device_(nullptr), started_(false) { LOG_ENTRY(); }

Worker::~Worker() {
  LOG_ENTRY();
  ZX_DEBUG_ASSERT(!started_.load());
}

void Worker::MakeRequest(zx_port_packet_t* packet, uint64_t op, void* arg) {
  static_assert(sizeof(uintptr_t) <= sizeof(uint64_t), "cannot store pointer as uint64_t");
  ZX_DEBUG_ASSERT(packet);
  packet->key = 0;
  packet->type = ZX_PKT_TYPE_USER;
  packet->status = ZX_OK;
  packet->user.u64[0] = op;
  packet->user.u64[1] = reinterpret_cast<uint64_t>(arg);
}

zx_status_t Worker::Start(Device* device, const DdkVolume& volume, zx::port&& port) {
  LOG_ENTRY_ARGS("device=%p, volume=%p, port=%p", device, &volume, &port);
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

  port_ = std::move(port);

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
  zx_status_t rc;

  zx_port_packet_t packet;
  while (true) {
    // Read request
    if ((rc = port_.wait(zx::time::infinite(), &packet)) != ZX_OK) {
      zxlogf(ERROR, "failed to read request: %s", zx_status_get_string(rc));
      return rc;
    }
    ZX_DEBUG_ASSERT(packet.key == 0);
    ZX_DEBUG_ASSERT(packet.type == ZX_PKT_TYPE_USER);
    ZX_DEBUG_ASSERT(packet.status == ZX_OK);

    // Handle control messages
    switch (packet.user.u64[0]) {
      case kBlockRequest:
        break;
      case kStopRequest:
        zxlogf(DEBUG, "worker %p stopping.", this);
        return ZX_OK;
      default:
        zxlogf(ERROR, "unknown request: 0x%016" PRIx64 "", packet.user.u64[0]);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Dispatch block request
    block_op_t* block = reinterpret_cast<block_op_t*>(packet.user.u64[1]);
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

  if (ZX_ROUNDDOWN(offset_vmo, ZX_PAGE_SIZE) != offset_vmo) {
    // Ensure the range inside the VMO we map is page aligned so that requests smaller than a page
    // still work.
    mapping_offset = offset_vmo - ZX_ROUNDDOWN(offset_vmo, ZX_PAGE_SIZE);
    offset_vmo = ZX_ROUNDDOWN(offset_vmo, ZX_PAGE_SIZE);
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
  auto cleanup = fbl::MakeAutoCall(
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
