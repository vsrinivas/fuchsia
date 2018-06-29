// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/port.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "device.h"
#include "extra.h"
#include "worker.h"

namespace zxcrypt {
namespace {

// Cap largest trasnaction to a quarter of the VMO buffer.
const uint32_t kMaxTransferSize = Volume::kBufferSize / 4;

// Kick off |Init| thread when binding.
int InitThread(void* arg) {
    return static_cast<Device*>(arg)->Init();
}

} // namespace

// Public methods

Device::Device(zx_device_t* parent)
    : DeviceType(parent), state_(0), info_(nullptr), hint_(0) {
    list_initialize(&queue_);
}

Device::~Device() {}

// Public methods called from global context

zx_status_t Device::Bind() {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!info_);

    // Add the (invisible) device to devmgr
    if ((rc = DdkAdd("zxcrypt", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
        zxlogf(ERROR, "DdkAdd('zxcrypt', DEVICE_ADD_INVISIBLE) failed: %s\n",
               zx_status_get_string(rc));
        return rc;
    }
    auto cleanup = fbl::MakeAutoCall([this] { DdkRemove(); });

    // Launch the init thread.
    if (thrd_create(&init_, InitThread, this) != thrd_success) {
        zxlogf(ERROR, "zxcrypt device %p initialization aborted: failed to start thread\n", this);
        return ZX_ERR_INTERNAL;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Device::Init() {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(!info_);

    zxlogf(TRACE, "zxcrypt device %p initializing\n", this);
    auto cleanup = fbl::MakeAutoCall([this]() {
        zxlogf(ERROR, "zxcrypt device %p failed to initialize\n", this);
        DdkRemove();
    });

    fbl::AllocChecker ac;
    fbl::unique_ptr<DeviceInfo> info(new (&ac) DeviceInfo());
    if (!ac.check()) {
        zxlogf(ERROR, "allocation failed: %zu bytes\n", sizeof(DeviceInfo));
        return ZX_ERR_NO_MEMORY;
    }
    info->base = nullptr;
    info->num_workers = 0;
    info_ = info.get();

    // Open the zxcrypt volume.  The volume may adjust the block info, so get it again and determine
    // the multiplicative factor needed to transform this device's blocks into its parent's.
    // TODO(security): ZX-1130 workaround.  Use null key of a fixed length until fixed
    crypto::Secret root_key;
    uint8_t* buf;
    if ((rc = root_key.Allocate(kZx1130KeyLen, &buf)) != ZX_OK) {
        return rc;
    }
    memset(buf, 0, root_key.len());
    fbl::unique_ptr<Volume> volume;
    if ((rc = Volume::Unlock(parent(), root_key, 0, &volume)) != ZX_OK) {
        return rc;
    }

    // Get the parent device's block interface
    block_info_t blk;
    if ((rc = device_get_protocol(parent(), ZX_PROTOCOL_BLOCK, &info->proto)) != ZX_OK) {
        zxlogf(ERROR, "failed to get block protocol: %s\n", zx_status_get_string(rc));
        return rc;
    }
    info->proto.ops->query(info->proto.ctx, &blk, &info->op_size);

    // Save device sizes
    info->block_size = blk.block_size;
    info->op_size += sizeof(extra_op_t);
    info->reserved_blocks = volume->reserved_blocks();
    info->reserved_slices = volume->reserved_slices();

    // Reserve space for shadow I/O transactions
    if ((rc = zx::vmo::create(Volume::kBufferSize, 0, &info->vmo)) != ZX_OK) {
        zxlogf(ERROR, "zx::vmo::create failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    constexpr uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    uintptr_t address;
    if ((rc = zx::vmar::root_self().map(0, info->vmo, 0, Volume::kBufferSize, flags, &address)) !=
        ZX_OK) {
        zxlogf(ERROR, "zx::vmar::map failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    info->base = reinterpret_cast<uint8_t*>(address);

    // Set up allocation bitmap
    {
        fbl::AutoLock lock(&mtx_);
        rc = map_.Reset(Volume::kBufferSize / info->block_size);
    }
    if (rc != ZX_OK) {
        zxlogf(ERROR, "bitmap allocation failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    // Start workers
    // TODO(aarongreen): Investigate performance implications of adding more workers.
    if ((rc = zx::port::create(0, &port_)) != ZX_OK) {
        zxlogf(ERROR, "zx::port::create failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    for (size_t i = 0; i < kNumWorkers; ++i) {
        zx::port port;
        port_.duplicate(ZX_RIGHT_SAME_RIGHTS, &port);
        if ((rc = workers_[i].Start(this, *volume, fbl::move(port))) != ZX_OK) {
            return rc;
        }
        ++info->num_workers;
    }

    // |info_| now holds the pointer; it is reclaimed in |DdkRelease|.
    DeviceInfo* released __attribute__((unused)) = info.release();

    // Enable the device
    state_.store(kActive);
    DdkMakeVisible();
    zxlogf(TRACE, "zxcrypt device %p initialized\n", this);

    cleanup.cancel();
    return ZX_OK;
}

////////////////////////////////////////////////////////////////
// ddk::Device methods

zx_status_t Device::DdkIoctl(uint32_t op, const void* in, size_t in_len, void* out, size_t out_len,
                             size_t* actual) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(info_);

    // Modify inputs
    switch (op) {
    case IOCTL_BLOCK_FVM_EXTEND:
    case IOCTL_BLOCK_FVM_SHRINK: {
        extend_request_t mod;
        if (!in || in_len < sizeof(mod)) {
            zxlogf(ERROR, "bad parameter(s): in=%p, in_len=%zu\n", in, in_len);
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(&mod, in, sizeof(mod));
        mod.offset += info_->reserved_slices;
        rc = device_ioctl(parent(), op, &mod, sizeof(mod), out, out_len, actual);
        break;
    }
    case IOCTL_BLOCK_FVM_VSLICE_QUERY: {
        query_request_t mod;
        if (!in || in_len < sizeof(mod)) {
            zxlogf(ERROR, "bad parameter(s): in=%p, in_len=%zu\n", in, in_len);
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(&mod, in, sizeof(mod));
        for (size_t i = 0; i < mod.count; ++i) {
            mod.vslice_start[i] += info_->reserved_slices;
        }
        rc = device_ioctl(parent(), op, &mod, sizeof(mod), out, out_len, actual);
        break;
    }
    default:
        rc = device_ioctl(parent(), op, in, in_len, out, out_len, actual);
        break;
    }
    if (rc < 0) {
        zxlogf(ERROR, "parent device returned failure for ioctl %" PRIu32 ": %s\n", op,
               zx_status_get_string(rc));
        return rc;
    }

    // Modify outputs
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* mod = static_cast<block_info_t*>(out);
        mod->block_count -= info_->reserved_blocks;
        if (mod->max_transfer_size > kMaxTransferSize) {
            mod->max_transfer_size = kMaxTransferSize;
        }
        break;
    }
    case IOCTL_BLOCK_FVM_QUERY: {
        fvm_info_t* mod = static_cast<fvm_info_t*>(out);
        mod->vslice_count -= info_->reserved_slices;
        break;
    }
    default:
        break;
    }
    return ZX_OK;
}

zx_off_t Device::DdkGetSize() {
    zx_off_t reserved, size;
    if (mul_overflow(info_->block_size, info_->reserved_blocks, &reserved) ||
        sub_overflow(device_get_size(parent()), reserved, &size)) {
        zxlogf(ERROR, "device_get_size returned less than what has been reserved\n");
        return 0;
    }
    return size;
}

// TODO(aarongreen): See ZX-1138.  Currently, there's no good way to trigger
// this on demand.
void Device::DdkUnbind() {
    zxlogf(TRACE, "zxcrypt device %p unbinding\n", this);
    // Clear the active flag.  The state is only zero if |DdkUnbind| has been called and all
    // requests are complete.
    if (state_.fetch_and(~kActive) == kActive) {
        DdkRemove();
    }
}

void Device::DdkRelease() {
    zx_status_t rc;

    // One way or another we need to release the memory
    auto cleanup = fbl::MakeAutoCall([this]() {
        zxlogf(TRACE, "zxcrypt device %p released\n", this);
        delete this;
    });

    // Make sure |Init()| is complete
    thrd_join(init_, &rc);
    if (rc != ZX_OK) {
        zxlogf(WARN, "init thread returned %s\n", zx_status_get_string(rc));
    }

    // If we died early enough (e.g. OOM), this doesn't exist
    if (!info_) {
        return;
    }

    // Reclaim |info_| to ensure its memory is freed.
    fbl::unique_ptr<DeviceInfo> info(const_cast<DeviceInfo*>(info_));

    // Stop workers; send a stop message to each, then join each (possibly in different order).
    zx_port_packet_t packet;
    packet.key = 0;
    packet.type = ZX_PKT_TYPE_USER;
    packet.status = ZX_ERR_STOP;
    for (size_t i = 0; i < info->num_workers; ++i) {
        port_.queue(&packet);
    }
    for (size_t i = 0; i < info->num_workers; ++i) {
        workers_[i].Stop();
    }

    // Release write buffer
    const uintptr_t address = reinterpret_cast<uintptr_t>(info->base);
    if (address != 0 && (rc = zx::vmar::root_self().unmap(address, Volume::kBufferSize)) != ZX_OK) {
        zxlogf(WARN, "failed to unmap %" PRIu32 " bytes at %" PRIuPTR ": %s\n", Volume::kBufferSize,
               address, zx_status_get_string(rc));
    }
}

////////////////////////////////////////////////////////////////
// ddk::BlockProtocol methods

void Device::BlockQuery(block_info_t* out_info, size_t* out_op_size) {
    ZX_DEBUG_ASSERT(info_);
    info_->proto.ops->query(info_->proto.ctx, out_info, out_op_size);
    out_info->block_count -= info_->reserved_blocks;
    *out_op_size = info_->op_size;
}

void Device::BlockQueue(block_op_t* block) {
    ZX_DEBUG_ASSERT(info_);
    zxlogf(TRACE, "zxcrypt device %p processing I/O request %p\n", this, block);

    // Check if the device is active, and if so increment the count to accept this request. The
    // corresponding decrement is in |BlockComplete|; all requests that make it out of this loop
    // must go through that function.  If the |state_| indicates we can't accept the request,
    // complete it without using |BlockComplete| to avoid touching |state_|.
    uint32_t expected = kActive;
    uint32_t desired = expected + 1;
    while (!state_.compare_exchange_weak(&expected, desired, fbl::memory_order_seq_cst,
                                         fbl::memory_order_seq_cst)) {
        if ((expected & kActive) == 0) {
            block->completion_cb(block, ZX_ERR_BAD_STATE);
            return;
        }
        if ((expected & kMaxReqs) == kMaxReqs) {
            block->completion_cb(block, ZX_ERR_UNAVAILABLE);
            return;
        }
        desired = expected + 1;
    }

    // Initialize our extra space
    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    extra->Init();

    // Skip the reserved blocks
    uint32_t command = block->command & BLOCK_OP_MASK;
    if ((command == BLOCK_OP_READ || command == BLOCK_OP_WRITE) &&
        add_overflow(block->rw.offset_dev, info_->reserved_blocks, &block->rw.offset_dev)) {
        zxlogf(ERROR, "adjusted offset overflow: block->rw.offset_dev=%" PRIu64 "\n",
               block->rw.offset_dev);
        BlockComplete(block, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    // Queue write requests to get a portion of the write buffer, send all others to the device
    if (command == BLOCK_OP_WRITE) {
        EnqueueWrite(block);
    } else {
        BlockForward(block, ZX_OK);
    }
}

void Device::BlockForward(block_op_t* block, zx_status_t status) {
    ZX_DEBUG_ASSERT(info_);
    zxlogf(TRACE, "zxcrypt device %p sending I/O request %p to parent device\n", this, block);

    if (!block) {
        return;
    }
    if (status != ZX_OK) {
        BlockComplete(block, status);
        return;
    }
    // Check if the device is active (i.e. |DdkUnbind| has not been called).
    if ((state_.load() & kActive) == 0) {
        zxlogf(ERROR, "zxcrypt device %p is not active\n", this);
        BlockComplete(block, ZX_ERR_BAD_STATE);
        return;
    }
    // Save info that may change
    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    extra->length = block->rw.length;
    extra->offset_dev = block->rw.offset_dev;
    extra->completion_cb = block->completion_cb;
    extra->cookie = block->cookie;

    // Register ourselves as the callback
    block->completion_cb = BlockCallback;
    block->cookie = this;

    // Send the request to the parent device
    info_->proto.ops->queue(info_->proto.ctx, block);
}

void Device::BlockComplete(block_op_t* block, zx_status_t status) {
    ZX_DEBUG_ASSERT(info_);
    zxlogf(TRACE, "zxcrypt device %p completing I/O request %p\n", this, block);
    zx_status_t rc;

    // If a portion of the write buffer was allocated, release it.
    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    if (extra->data) {
        uint64_t off = (extra->data - info_->base) / info_->block_size;
        uint64_t len = block->rw.length;
        extra->data = nullptr;

        fbl::AutoLock lock(&mtx_);
        ZX_DEBUG_ASSERT(map_.Get(off, off + len));
        rc = map_.Clear(off, off + len);
        ZX_DEBUG_ASSERT(rc == ZX_OK);
    }

    // Complete the request.
    block->completion_cb(block, status);

    // If we previously stalled, try to re-queue the deferred requests; otherwise, avoid taking the
    // lock.
    if (state_.fetch_and(~kStalled) & kStalled) {
        EnqueueWrite();
    }

    // Decrement the reference count.  It can only hit zero if |DdkUnbind| has been called and all
    // requests are complete.
    if (state_.fetch_sub(1) == 1) {
        DdkRemove();
    }
}

////////////////////////////////////////////////////////////////
// Private methods

void Device::EnqueueWrite(block_op_t* block) {
    zx_status_t rc = ZX_OK;

    fbl::AutoLock lock(&mtx_);

    // Append the request to the write queue (if not null)
    extra_op_t* extra;
    if (block) {
        extra = BlockToExtra(block, info_->op_size);
        list_add_tail(&queue_, &extra->node);
    }

    // If we previously stalled and haven't completed any requests since then, don't bother looking
    // for space again.
    if (state_.load() & kStalled) {
        return;
    }

    // Process as many pending write requests as we can right now.
    list_node_t pending;
    list_initialize(&pending);
    while (!list_is_empty(&queue_)) {
        extra = list_peek_head_type(&queue_, extra_op_t, node);
        block = ExtraToBlock(extra, info_->op_size);

        // Find an available offset in the write buffer
        uint64_t off;
        uint64_t len = block->rw.length;
        if ((rc = map_.Find(false, hint_, map_.size(), len, &off)) == ZX_ERR_NO_RESOURCES &&
            (rc = map_.Find(false, 0, map_.size(), len, &off)) == ZX_ERR_NO_RESOURCES) {
            zxlogf(TRACE, "zxcrypt device %p stalled pending request completion\n", this);
            state_.fetch_or(kStalled);
            break;
        }

        // We don't expect any other errors
        ZX_DEBUG_ASSERT(rc == ZX_OK);
        rc = map_.Set(off, off + len);
        ZX_DEBUG_ASSERT(rc == ZX_OK);

        // Save a hint as to where to start looking next time
        hint_ = (off + len) % map_.size();

        // Modify request to use write buffer
        extra->data = info_->base + (off * info_->block_size);
        extra->vmo = block->rw.vmo;
        extra->offset_vmo = block->rw.offset_vmo;

        block->rw.vmo = info_->vmo.get();
        block->rw.offset_vmo = (extra->data - info_->base) / info_->block_size;

        list_add_tail(&pending, list_remove_head(&queue_));
    }

    // Release the lock and send blocks that are ready to the workers
    lock.release();
    extra_op_t* tmp;
    list_for_every_entry_safe (&pending, extra, tmp, extra_op_t, node) {
        list_delete(&extra->node);
        block = ExtraToBlock(extra, info_->op_size);
        SendToWorker(block);
    }
}

void Device::SendToWorker(block_op_t* block) {
    zx_status_t rc;
    zx_port_packet_t packet;
    packet.key = 0;
    packet.type = ZX_PKT_TYPE_USER;
    packet.status = ZX_ERR_NEXT;
    memcpy(packet.user.c8, &block, sizeof(block));
    zxlogf(TRACE, "zxcrypt device %p sending I/O request %p to workers\n", this, block);
    if ((rc = port_.queue(&packet)) != ZX_OK) {
        zxlogf(ERROR, "zx::port::queue failed: %s\n", zx_status_get_string(rc));
        BlockComplete(block, rc);
        return;
    }
}

void Device::BlockCallback(block_op_t* block, zx_status_t status) {
    Device* device = static_cast<Device*>(block->cookie);
    zxlogf(TRACE, "zxcrypt device %p received I/O response %p\n", device, block);

    // Restore data that may have changed
    extra_op_t* extra = BlockToExtra(block, device->op_size());
    block->rw.length = extra->length;
    block->rw.offset_dev = extra->offset_dev;
    block->completion_cb = extra->completion_cb;
    block->cookie = extra->cookie;

    // If this is a successful read, send it to the workers
    if (status == ZX_OK && (block->command & BLOCK_OP_MASK) == BLOCK_OP_READ) {
        device->SendToWorker(block);
    } else {
        device->BlockComplete(block, status);
    }
}

} // namespace zxcrypt

extern "C" zx_status_t zxcrypt_device_bind(void* ctx, zx_device_t* parent) {
    zx_status_t rc;
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<zxcrypt::Device>(&ac, parent);
    if (!ac.check()) {
        zxlogf(ERROR, "allocation failed: %zu bytes\n", sizeof(zxcrypt::Device));
        return ZX_ERR_NO_MEMORY;
    }
    if ((rc = dev->Bind()) != ZX_OK) {
        return rc;
    }
    // devmgr is now in charge of the memory for |dev|
    zxcrypt::Device* devmgr_owned __attribute__((unused));
    devmgr_owned = dev.release();
    return ZX_OK;
}
