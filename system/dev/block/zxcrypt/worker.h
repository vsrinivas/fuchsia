// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <threads.h>

#include <crypto/cipher.h>
#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>
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

    // Opcodes for requests that can be sent to workers.
    static constexpr uint64_t kBlockRequest = 0x1;
    static constexpr uint64_t kStopRequest = 0x2;

    // Configure the given |packet| to be an |op| request, with an optional |arg|.
    static void MakeRequest(zx_port_packet_t* packet, uint64_t op, void* arg = nullptr);

    // Starts the worker, which will service requests sent from the given |device| on the given
    // |port|.  Cryptographic operations will use the key material from the given |volume|.
    zx_status_t Start(Device* device, const Volume& volume, zx::port&& port);

    // Asks the worker to stop.  This call blocks until the worker has finished processing the
    // currently queued operations and exits.
    zx_status_t Stop();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Worker);

    // Loop thread.  Reads an I/O request from the |port_| and dispatches it between |EncryptWrite|
    // and |DecryptRead|.
    static int WorkerRun(void* arg) { return static_cast<Worker*>(arg)->Run(); }
    zx_status_t Run();

    // Copies the plaintext data to be written to the write buffer location given in |block|'s extra
    // information, and encrypts it before sending it to the parent device.
    zx_status_t EncryptWrite(block_op_t* block);

    // Maps the ciphertext data in |block|, and decrypts it in place before completing the block op.
    zx_status_t DecryptRead(block_op_t* block);

    // The cipher objects used to perform cryptographic.  See notes on "random access" in
    // crypto/cipher.h.
    crypto::Cipher encrypt_;
    crypto::Cipher decrypt_;

    // The device associated with this worker.
    Device* device_;

    // The port to wait for I/O request on, as given by the device.
    zx::port port_;

    // The executing thread for this worker
    thrd_t thrd_;
};

} // namespace zxcrypt
