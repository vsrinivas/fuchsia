// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <crypto/cipher.h>
#include <ddk/device.h>
#include <ddk/protocol/block.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <fbl/atomic.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <lib/zx/port.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include "extra.h"
#include "worker.h"

namespace zxcrypt {

// See ddk::Device in ddktl/device.h
class Device;
using DeviceType = ddk::Device<Device, ddk::Ioctlable, ddk::GetSizable, ddk::Unbindable>;

// |zxcrypt::Device| is an encrypted block device filter driver.  It binds to a block device and
// transparently encrypts writes to/decrypts reads from that device.  It shadows incoming requests
// with its own |zxcrypt::Op| request structure that uses a mapped VMO as working memory for
// cryptographic transformations.
class Device final : public DeviceType, public ddk::BlockProtocol<Device> {
public:
    explicit Device(zx_device_t* parent);
    ~Device();

    // Publish some constants for the workers
    inline uint32_t block_size() const { return info_->block_size; }
    inline size_t op_size() const { return info_->op_size; }

    // Called via ioctl_device_bind.  This method sets up the synchronization primitives and starts
    // the |Init| thread.
    zx_status_t Bind();

    // The body of the |Init| thread.  This method attempts to cryptographically unseal the device
    // for normal operation, and adds it to the device tree if successful.
    zx_status_t Init() __TA_EXCLUDES(mtx_);

    // ddk::Device methods; see ddktl/device.h
    zx_status_t DdkIoctl(uint32_t op, const void* in, size_t in_len, void* out, size_t out_len,
                         size_t* actual);
    zx_off_t DdkGetSize();
    void DdkUnbind();
    void DdkRelease();

    // ddk::BlockProtocol methods; see ddktl/protocol/block.h
    void BlockQuery(block_info_t* out_info, size_t* out_op_size);
    void BlockQueue(block_op_t* block) __TA_EXCLUDES(mtx_);

    // If |status| is |ZX_OK|, sends |block| to the parent block device; otherwise calls
    // |BlockComplete| on the |block|. Uses the extra space following the |block| to save fields
    // which may be modified, including the |completion_cb|, which it sets to |BlockCallback|.
    void BlockForward(block_op_t* block, zx_status_t status) __TA_EXCLUDES(mtx_);

    // Returns a completed |block| request to the caller of |BlockQueue|.
    void BlockComplete(block_op_t* block, zx_status_t status) __TA_EXCLUDES(mtx_);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

    // TODO(aarongreen): Investigate performance impact of changing this.
    // Number of encrypting/decrypting workers
    static const size_t kNumWorkers = 2;

    // Adds |block| to the write queue if not null, and sends to the workers as many write requests
    // as fit in the space available in the write buffer.
    void EnqueueWrite(block_op_t* block = nullptr) __TA_EXCLUDES(mtx_);

    // Sends a block I/O request to a worker to be encrypted or decrypted.
    void SendToWorker(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Callback used for block ops sent to the parent device.  Restores the fields saved by
    // |BlockForward|.
    static void BlockCallback(block_op_t* block, zx_status_t status);

    // Device state.  This atomic is a combination of bit flags and a counter for the number of
    // outstanding requests.  These are combined to allow accessing both in a lock free manner; e.g.
    // "if the device is active, increment the request count" becomes a read-modify-write operation.
    // The role of each bit is represented by one of the constants below, and is as follows:
    //
    //   Bit 31:     Set if device is active, i.e. |Init| has been called but |DdkUnbind| hasn't.
    //                 I/O requests to |BlockQueue| are immediately completed with
    //                 |ZX_ERR_BAD_STATE| if this is not set.
    //
    //   Bit 30:     Set if writes are stalled, i.e.  a write request was deferred due to lack of
    //                 space in the write buffer, and no requests have since completed.
    //
    //   Bits 29-24: Reserved.
    //
    //   Bits 23-0:  Number of accepted requests waiting to be completed.  When this limit is
    //                 reached, additional I/O requests to |BlockQueue| will be completed with
    //                |ZX_ERR_BAD_STATE|.
    //
    fbl::atomic_uint32_t state_;
    static constexpr uint32_t kActive = 1U << 31;
    static constexpr uint32_t kStalled = 1U << 30;
    static constexpr uint32_t kMaxReqs = 0x00FFFFFF;

    // This struct bundles several commonly accessed fields.  The bare pointer IS owned by the
    // object; it's "constness" prevents it from being an automatic pointer but allows it to be used
    // without holding the lock.  It is allocated and "constified" in |Init|, and |DdkRelease| must
    // "deconstify" and free it.  Its nullity is also used as an indicator whether |Init()| has been
    // called.
    struct DeviceInfo {
        // The parent device's block information
        uint32_t block_size;
        // The parent device's required block_op_t size.
        size_t op_size;
        // Callbacks to the parent's block protocol methods.
        block_protocol_t proto;
        // The number of blocks reserved for metadata.
        uint64_t reserved_blocks;
        // The number of slices reserved for metadata.
        uint64_t reserved_slices;
        // A memory region used when encrypting write transactions.
        zx::vmo vmo;
        // Base address of the VMAR backing the VMO.
        uint8_t* base;
        // Number of workers actually running.
        uint32_t num_workers;
    };
    const DeviceInfo* info_;

    // The |Init| thread, used to configure and add the device.
    thrd_t init_;

    // Threads that performs encryption/decryption.
    Worker workers_[kNumWorkers];

    // Port used to send write/read operations to be encrypted/decrypted.
    zx::port port_;

    // Primary lock for accessing the write queue
    fbl::Mutex mtx_;

    // Indicates which blocks of the write buffer are in use.
    bitmap::RawBitmapGeneric<bitmap::DefaultStorage> map_ __TA_GUARDED(mtx_);

    // Describes a queue of deferred block requests.
    list_node_t queue_ __TA_GUARDED(mtx_);

    // Hint as to where in the bitmap to begin looking for available space.
    size_t hint_ __TA_GUARDED(mtx_);
};

} // namespace zxcrypt
