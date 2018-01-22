// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <crypto/cipher.h>
#include <fdio/debug.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "device.h"
#include "extra.h"
#include "worker.h"

#define ZXDEBUG 0

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
        xprintf("bad parameters: device=%p\n", device);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((rc = volume.Bind(crypto::Cipher::kEncrypt, &encrypt_)) != ZX_OK ||
        (rc = volume.Bind(crypto::Cipher::kDecrypt, &decrypt_)) != ZX_OK) {
        return rc;
    }
    block_info_t info;
    if ((rc = volume.GetBlockInfo(&info)) != ZX_OK) {
        return rc;
    }

    device_ = device;
    port.duplicate(ZX_RIGHT_SAME_RIGHTS, &port_);

    if (thrd_create(&thrd_, Thread, this) != thrd_success) {
        xprintf("failed to start thread\n");
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t Worker::Loop() {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(device_);
    zx_port_packet_t packet;
    while (port_.wait(zx::time::infinite(), &packet, 1) == ZX_OK && packet.status == ZX_ERR_NEXT) {
        block_op_t* block = reinterpret_cast<block_op_t*>(packet.user.u64[0]);
        extra_op_t* ex = device_->BlockToExtra(block);
        switch (block->command & BLOCK_OP_MASK) {
        case BLOCK_OP_WRITE:
            if ((rc = zx_vmo_read(ex->vmo, ex->buf, ex->off, ex->len)) != ZX_OK ||
                (rc = encrypt_.Encrypt(ex->buf, ex->num, ex->len, ex->buf) != ZX_OK)) {
                device_->BlockRelease(block, rc);
                break;
            }
            device_->BlockForward(block);
            break;

        case BLOCK_OP_READ:
            if ((rc = decrypt_.Decrypt(ex->buf, ex->num, ex->len, ex->buf)) != ZX_OK ||
                (rc = zx_vmo_write(ex->vmo, ex->buf, ex->off, ex->len)) != ZX_OK) {
                device_->BlockRelease(block, rc);
                break;
            }
            device_->BlockRelease(block, ZX_OK);
            break;

        default:
            device_->BlockRelease(block, ZX_ERR_NOT_SUPPORTED);
        }
    }
    return ZX_OK;
}

zx_status_t Worker::Stop() {
    zx_status_t rc;

    thrd_join(thrd_, &rc);

    return rc;
}

} // namespace zxcrypt
