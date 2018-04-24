// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>

#include <crypto/cipher.h>
#include <lib/zx/port.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "extra.h"

namespace zxcrypt {

class Device;

// |zxcrypt::Worker| represents a thread performing cryptographic transformations on block I/O data.
// Since these operations may have significant and asymmetric costs between encrypting and
// decrypting, they are performed asynchronously on separate threads.  The |zxcrypt::Device| may
// spin up multiple workers pulling from a shared queues to optimize the throughput.
class Worker final {
public:
    Worker();
    ~Worker();

    // Starts the worker, which will service requests pulled from the given |queue| for the given
    // |device|.  Cryptographic operations will use the region of memory starting at the given
    // |base|, and the ciphers will be initialized according to key data stored in the given
    // |volume|.
    zx_status_t Start(Device* device, const Volume& volume, const zx::port& port);

    // Thread body. Encrypts write requests and and forwards them to the parent device using
    // Device::BlockForward.  Decrypts read responses and completes them back to the requester using
    // Device::BlockRelease.  This method should not be called directly; use |Start| instead.
    zx_status_t Loop();

    // Asks the worker to stop.  This call blocks until the worker has finished processing the
    // currently queued operations and exits.
    zx_status_t Stop();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Worker);

    // The cipher objects used to perform cryptographic.  See notes on "random access" in
    // crypto/cipher.h.
    crypto::Cipher encrypt_;
    crypto::Cipher decrypt_;
    // The device associated with this worker.
    Device* device_;
    // The port to wait for I/O request on.  Duplicate of the device's port.
    zx::port port_;
    // The executing thread for this worker
    thrd_t thrd_;
};

} // namespace zxcrypt
