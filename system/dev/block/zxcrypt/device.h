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
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <lib/zx/port.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <zircon/listnode.h>

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

    // Called via ioctl_device_bind.  This method sets up the synchronization primitives and starts
    // the |Init| thread.
    zx_status_t Bind();

    // The body of the |Init| thread.  This method attempts to cryptographically unseal the device
    // for normal operation, and adds it to the device tree if successful.
    zx_status_t Init() __TA_EXCLUDES(mtx_);

    // ddk::Device methods; see ddktl/device.h
    zx_status_t DdkIoctl(uint32_t op, const void* in, size_t in_len, void* out, size_t out_len,
                         size_t* actual) __TA_EXCLUDES(mtx_);
    zx_off_t DdkGetSize() __TA_EXCLUDES(mtx_);
    void DdkUnbind() __TA_EXCLUDES(mtx_);
    void DdkRelease() __TA_EXCLUDES(mtx_);

    // ddk::BlockProtocol methods; see ddktl/protocol/block.h
    void BlockQuery(block_info_t* out_info, size_t* out_op_size) __TA_EXCLUDES(mtx_);
    void BlockQueue(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Send |block| to the parent of the device stored in |txn->cookie|.
    void BlockForward(block_op_t* block) __TA_EXCLUDES(mtx_);

    // I/O callback invoked by the parent device.  Stored in |block->completion_cb| by |BlockQueue|.
    static void BlockComplete(block_op_t* block, zx_status_t rc) __TA_EXCLUDES(mtx_);

    // Completes a |block| returning from the parent device stored in |txn->cookie| and returns it
    // to the caller of |DdkIotxnQueue|.
    void BlockRelease(block_op_t* block, zx_status_t rc) __TA_EXCLUDES(mtx_);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Device);

    // TODO(aarongreen): Investigate performance impact of changing this.
    // Number of encrypting/decrypting workers
    static const size_t kNumWorkers = 2;

    // Increments the amount of work this device has outstanding.  This should be called at the
    // start of any method that shouldn't be invoked after the device has been unbound.  It notably
    // is called in |Init| to prevent the work from dropping to zero before |DdkUnbind| is called.
    // Returns ZX_ERR_BAD_STATE if |DdkUnbind| has been called.
    zx_status_t AddTaskLocked() __TA_REQUIRES(mtx_);

    // Decrements the amount of work this task has outstanding.  This should be called whenever the
    // work started with a call to |addTask| completes.  When the amount of work reaches zero,
    // |DdkRemove| is called.  It notably is called in |DdkUnbind| to cause |DdkRemove| to be called
    // automatically once the device finishes its current workload.
    void FinishTaskLocked() __TA_REQUIRES(mtx_);

    // Attempts to reserve space in the working buffer to process |block|.  If space can't be found,
    // returns ZX_ERR_SHOULD_WAIT. In this case, the block can be re-queued using |BlockRequeue|
    // when resources are available.
    zx_status_t BlockAcquire(block_op_t* block);

    // Attempts to reserve space in the working buffer to process |len| blocks.  If space can't be
    // found returns ZX_ERR_SHOULD_WAIT, otherwise it fills in |extra| with the details of the
    // reserved space.
    zx_status_t BlockAcquireLocked(uint64_t len, extra_op_t* extra) __TA_REQUIRES(mtx_);

    // Attempts to acquire resources for a previously deferred request.  Returns:
    //  |ZX_ERR_NEXT| if successful and |ProcessBlock| can be called with |out_block| and |out_off|.
    //  |ZX_ERR_STOP| if there are no outstanding deferred requests
    //  |ZX_ERR_SHOULD_WAIT| if there still aren't enough resources available.
    zx_status_t BlockRequeue(block_op_t** out_block);

    // Take a |block|, prepare it to be transformed, and send it to a worker.
    void ProcessBlock(block_op_t* block) __TA_EXCLUDES(mtx_);

    // Marks the blocks from |off| to |off + len| as available.  Signals waiting callers if
    // |AcquireBlocks| previously returned ZX_ERR_SHOULD_WAIT.
    void ReleaseBlock(extra_op_t* extra) __TA_EXCLUDES(mtx_);

    // Unsynchronized fields

    // This struct bundles several commonly accessed fields.  The bare pointer IS owned by the
    // object; it's "constness" prevents it from being an automatic pointer but allows it to be used
    // without holding the lock.  It is allocated and "constified" in |Init|, and |DdkRelease| must
    // "deconstify" and free it.
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
        // A memory region used when encrypting/decrypting I/O transactions.
        zx::vmo vmo;
    };
    const DeviceInfo* info_;

    // Thread-related fields

    // The |Init| thread, used to configure and add the device.
    thrd_t init_;
    // Threads that performs encryption/decryption.
    Worker workers_[kNumWorkers];
    // Port used to send write/read operations to be encrypted/decrypted.
    zx::port port_;
    // Primary lock for accessing the fields below
    fbl::Mutex mtx_;

    // Synchronized fields

    // Indicates whether this object is ready for I/O.
    bool active_ __TA_GUARDED(mtx_);
    // The number of outstanding tasks.  See |AddTask| and |FinishTask|.
    uint64_t tasks_;
    // Base address of the VMAR backing |rw_.vmo|.
    uintptr_t mapped_ __TA_GUARDED(mtx_);
    uint8_t* base_;

    // Indicates which ops (and corresponding blocks in the VMO) are in use.
    bitmap::RawBitmapGeneric<bitmap::DefaultStorage> map_ __TA_GUARDED(mtx_);
    // Offset in the bitmap of the most recently allocated bit.
    size_t last_ __TA_GUARDED(mtx_);

    // Describes a queue of deferred block requests.
    list_node_t queue_ __TA_GUARDED(mtx_);
};

} // namespace zxcrypt
