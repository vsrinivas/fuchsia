// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <crypto/cipher.h>
#include <ddk/debug.h>
#include <lib/zx/port.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "device.h"
#include "extra.h"
#include "worker.h"

namespace zxcrypt {
namespace {

int Thread(void* arg) {
    return static_cast<Worker*>(arg)->Loop();
}

} // namespace

Worker::Worker() : device_(nullptr) {}

Worker::~Worker() {}

zx_status_t Worker::Start(Device* device, const Volume& volume, const zx::port& port) {
    zx_status_t rc;

    if (!device) {
        zxlogf(ERROR, "bad parameters: device=%p\n", device);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((rc = volume.Bind(crypto::Cipher::kEncrypt, &encrypt_)) != ZX_OK ||
        (rc = volume.Bind(crypto::Cipher::kDecrypt, &decrypt_)) != ZX_OK) {
        return rc;
    }
    device_ = device;
    port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_);

    if (thrd_create(&thrd_, Thread, this) != thrd_success) {
        zxlogf(ERROR, "failed to start thread\n");
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t Worker::Loop() {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(device_);
    zx_port_packet_t packet;

    // Use the first request as a signal that the device is ready
    if ((rc = port_.wait(zx::time::infinite(), &packet)) != ZX_OK ||
        packet.status != ZX_ERR_NEXT) {
        zxlogf(ERROR, "failed to start worker: %s\n", zx_status_get_string(rc));
        return rc;
    }

    block_info_t info;
    size_t op_size;
    device_->BlockQuery(&info, &op_size);

    do {
        block_op_t* block = reinterpret_cast<block_op_t*>(packet.user.u64[0]);
        extra_op_t* extra = BlockToExtra(block, op_size);

        uint32_t length;
        uint64_t offset_dev, offset_vmo;
        if (mul_overflow(extra->length, info.block_size, &length) ||
            mul_overflow(extra->offset_dev, info.block_size, &offset_dev) ||
            mul_overflow(extra->offset_vmo, info.block_size, &offset_vmo)) {
            device_->BlockRelease(block, ZX_ERR_OUT_OF_RANGE);
            continue;
        }

        switch (block->command & BLOCK_OP_MASK) {
        case BLOCK_OP_WRITE:
            if ((rc = zx_vmo_read(extra->vmo, extra->data, offset_vmo, length)) != ZX_OK ||
                (rc = encrypt_.Encrypt(extra->data, offset_dev, length, extra->data) != ZX_OK)) {
                device_->BlockRelease(block, rc);
                continue;
            }
            device_->BlockForward(block);
            break;

        case BLOCK_OP_READ:
            if ((rc = decrypt_.Decrypt(extra->data, offset_dev, length, extra->data)) != ZX_OK ||
                (rc = zx_vmo_write(extra->vmo, extra->data, offset_vmo, length)) != ZX_OK) {
                device_->BlockRelease(block, rc);
                continue;
            }
            device_->BlockRelease(block, ZX_OK);
            break;

        default:
            device_->BlockRelease(block, ZX_ERR_NOT_SUPPORTED);
        }
    } while ((rc = port_.wait(zx::time::infinite(), &packet)) == ZX_OK &&
             packet.status == ZX_ERR_NEXT);
    return rc;
}

zx_status_t Worker::Stop() {
    zx_status_t rc;

    thrd_join(thrd_, &rc);

    return rc;
}

} // namespace zxcrypt
