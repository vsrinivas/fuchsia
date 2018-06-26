// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <crypto/cipher.h>
#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <lib/zx/port.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "debug.h"
#include "device.h"
#include "extra.h"
#include "worker.h"

namespace zxcrypt {
namespace {

int WorkerRun(void* arg) {
    return static_cast<Worker*>(arg)->Loop();
}

} // namespace

Worker::Worker() : device_(nullptr) {
    LOG_ENTRY();
}

Worker::~Worker() {
    LOG_ENTRY();
}

zx_status_t Worker::Start(Device* device, const Volume& volume, zx::port&& port) {
    LOG_ENTRY_ARGS("device=%p, volume=%p, port=%p", device, &volume, &port);
    zx_status_t rc;

    if (!device) {
        zxlogf(ERROR, "bad parameters: device=%p\n", device);
        return ZX_ERR_INVALID_ARGS;
    }
    device_ = device;

    if ((rc = volume.Bind(crypto::Cipher::kEncrypt, &encrypt_)) != ZX_OK ||
        (rc = volume.Bind(crypto::Cipher::kDecrypt, &decrypt_)) != ZX_OK) {
        zxlogf(ERROR, "failed to bind ciphers: %s\n", zx_status_get_string(rc));
        return rc;
    }

    port_ = fbl::move(port);

    if (thrd_create(&thrd_, WorkerRun, this) != thrd_success) {
        zxlogf(ERROR, "failed to start thread\n");
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t Worker::Loop() {
    LOG_ENTRY();
    ZX_DEBUG_ASSERT(device_);
    zx_status_t rc;

    zx_port_packet_t packet;
    while (true) {
        // Read request
        if ((rc = port_.wait(zx::time::infinite(), &packet)) != ZX_OK) {
            zxlogf(ERROR, "failed to read request: %s\n", zx_status_get_string(rc));
            return rc;
        }
        if (packet.status == ZX_ERR_STOP) {
            zxlogf(TRACE, "worker %p stopping.\n", this);
            return ZX_OK;
        }

        // Dispatch request
        block_op_t* block = reinterpret_cast<block_op_t*>(packet.user.u64[0]);
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
        zxlogf(ERROR,
               "overflow; length=%" PRIu32 "; offset_dev=%" PRIu64 "; offset_vmo=%" PRIu64 "\n",
               block->rw.length, block->rw.offset_dev, extra->offset_vmo);
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Copy and encrypt the plaintext
    if ((rc = zx_vmo_read(extra->vmo, extra->data, offset_vmo, length)) != ZX_OK) {
        zxlogf(ERROR, "zx_vmo_read() failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    if ((rc = encrypt_.Encrypt(extra->data, offset_dev, length, extra->data) != ZX_OK)) {
        zxlogf(ERROR, "failed to encrypt: %s\n", zx_status_get_string(rc));
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
    if (mul_overflow(block->rw.length, device_->block_size(), &length) ||
        mul_overflow(block->rw.offset_dev, device_->block_size(), &offset_dev) ||
        mul_overflow(block->rw.offset_vmo, device_->block_size(), &offset_vmo)) {
        zxlogf(ERROR,
               "overflow; length=%" PRIu32 "; offset_dev=%" PRIu64 "; offset_vmo=%" PRIu64 "\n",
               block->rw.length, block->rw.offset_dev, block->rw.offset_vmo);
        return ZX_ERR_OUT_OF_RANGE;
    }

    // Map the ciphertext
    zx_handle_t root = zx_vmar_root_self();
    uintptr_t address;
    constexpr uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    if ((rc = zx_vmar_map(root, 0, block->rw.vmo, offset_vmo, length, flags, &address)) != ZX_OK) {
        zxlogf(ERROR, "zx::vmar::root_self()->map() failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    auto cleanup =
        fbl::MakeAutoCall([root, address, length]() { zx_vmar_unmap(root, address, length); });

    // Decrypt in place
    uint8_t* data = reinterpret_cast<uint8_t*>(address);
    if ((rc = decrypt_.Decrypt(data, offset_dev, length, data)) != ZX_OK) {
        zxlogf(ERROR, "failed to decrypt: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t Worker::Stop() {
    LOG_ENTRY();
    zx_status_t rc;

    thrd_join(thrd_, &rc);

    if (rc != ZX_OK) {
        zxlogf(WARN, "worker exited with error: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

} // namespace zxcrypt
