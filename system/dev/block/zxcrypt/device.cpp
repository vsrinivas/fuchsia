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
    : DeviceType(parent), info_(nullptr), active_(false), tasks_(0), mapped_(0), base_(nullptr),
      last_(0), head_(nullptr), tail_(nullptr) {}

Device::~Device() {}

// Public methods called from global context

zx_status_t Device::Bind() {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);

    // Launch the init thread.
    if (thrd_create(&init_, InitThread, this) != thrd_success) {
        zxlogf(ERROR, "zxcrypt device %p initialization aborted: failed to start thread\n", this);
        return ZX_ERR_INTERNAL;
    }

    // Add the (invisible) device to devmgr
    if ((rc = DdkAdd("zxcrypt", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
        zxlogf(ERROR, "DdkAdd('zxcrypt', DEVICE_ADD_INVISIBLE) failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t Device::Init() {
    zx_status_t rc;

    zxlogf(INFO, "zxcrypt device %p initializing\n", this);
    fbl::AutoLock lock(&mtx_);
    // We make an extra call to |AddTask| to ensure the counter never goes to zero before the
    // corresponding extra call to |FinishTask| in |DdkUnbind|.
    active_ = true;
    AddTaskLocked();

    // Clang gets confused and thinks the thread isn't holding the lock
    auto cleanup = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        zxlogf(ERROR, "zxcrypt device %p failed to initialize\n", this);
        lock.release();
        DdkUnbind();
    });

    fbl::AllocChecker ac;
    fbl::unique_ptr<DeviceInfo> info(new (&ac) DeviceInfo);
    if (!ac.check()) {
        zxlogf(ERROR, "allocation failed: %zu bytes\n", sizeof(DeviceInfo));
        return ZX_ERR_NO_MEMORY;
    }

    // Open the zxcrypt volume.  The volume may adjust the block info, so get it again and determine
    // the multiplicative factor needed to transform this device's blocks into its parent's.
    // TODO(security): ZX-1130 workaround.  Use null key of a fixed length until fixed
    crypto::Secret root_key;
    uint8_t *buf;
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
    if ((rc = zx::vmar::root_self().map(0, info->vmo, 0, Volume::kBufferSize,
                                        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &mapped_)) !=
        ZX_OK) {
        zxlogf(ERROR, "zx::vmar::map failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    base_ = reinterpret_cast<uint8_t*>(mapped_);
    if ((rc = map_.Reset(Volume::kBufferSize / info->block_size)) != ZX_OK) {
        zxlogf(ERROR, "bitmap allocation failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    // TODO(aarongreen): Investigate performance implications of adding more workers.
    if ((rc = zx::port::create(0, &port_)) != ZX_OK) {
        zxlogf(ERROR, "zx::port::create failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    for (size_t i = 0; i < kNumWorkers; ++i) {
        if ((rc = workers_[i].Start(this, *volume, port_)) != ZX_OK) {
            return rc;
        }
    }

    // Make the pointer const
    info_ = info.release();
    DdkMakeVisible();
    zxlogf(INFO, "zxcrypt device %p initialized\n", this);

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
    block_info_t blk;
    size_t ignored;
    BlockQuery(&blk, &ignored);
    return blk.block_count * blk.block_size;
}

// TODO(aarongreen): See ZX-1138.  Currently, there's no good way to trigger
// this on demand.
void Device::DdkUnbind() {
    zxlogf(INFO, "zxcrypt device %p unbinding\n", this);
    fbl::AutoLock lock(&mtx_);
    active_ = false;
    if (port_.is_valid()) {
        zx_port_packet_t packet;
        packet.key = 0;
        packet.type = ZX_PKT_TYPE_USER;
        packet.status = ZX_ERR_STOP;
        for (size_t i = 0; i < kNumWorkers; ++i) {
            port_.queue(&packet);
        }
        port_.reset();
    }
    // See |Init|; this is the "extra" call to |FinishTask|.
    FinishTaskLocked();
}

void Device::DdkRelease() {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);
    thrd_join(init_, &rc);
    if (rc != ZX_OK) {
        zxlogf(ERROR, "WARNING: init thread returned %s\n", zx_status_get_string(rc));
    }
    for (size_t i = 0; i < kNumWorkers; ++i) {
        workers_[i].Stop();
    }
    if (mapped_ != 0 && (rc = zx::vmar::root_self().unmap(mapped_, Volume::kBufferSize)) != ZX_OK) {
        zxlogf(ERROR, "WARNING: failed to unmap %" PRIu32 " bytes at %" PRIuPTR ": %s\n",
               Volume::kBufferSize, mapped_, zx_status_get_string(rc));
    }
    fbl::unique_ptr<DeviceInfo> info(const_cast<DeviceInfo*>(info_));
    info_ = nullptr;
    zxlogf(INFO, "zxcrypt device %p released\n", this);
    lock.release();
    delete this;
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
    zx_status_t rc;
    ZX_DEBUG_ASSERT(info_);

    switch (block->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        break;
    default:
        // Pass-through to parent
        BlockForward(block);
        return;
    }

    // Ignore zero-length I/O
    if (block->rw.length == 0) {
        block->completion_cb(block, ZX_OK);
        return;
    }

    // Reserve space to do cryptographic transformations
    rc = BlockAcquire(block);
    switch (rc) {
    case ZX_OK:
        ProcessBlock(block);
        break;
    case ZX_ERR_SHOULD_WAIT:
        break;
    default:
        block->completion_cb(block, rc);
    }
}

void Device::BlockForward(block_op_t* block) {
    ZX_DEBUG_ASSERT(info_);
    info_->proto.ops->queue(info_->proto.ctx, block);
}

void Device::BlockComplete(block_op_t* block, zx_status_t rc) {
    Device* device = static_cast<Device*>(block->cookie);

    if (rc != ZX_OK || (block->command & BLOCK_OP_MASK) != BLOCK_OP_READ) {
        device->BlockRelease(block, rc);
        return;
    }

    zx_port_packet_t packet;
    packet.key = 0;
    packet.type = ZX_PKT_TYPE_USER;
    packet.status = ZX_ERR_NEXT;
    memcpy(packet.user.c8, &block, sizeof(block));
    if ((rc = device->port_.queue(&packet)) != ZX_OK) {
        device->BlockRelease(block, rc);
    }
}

void Device::BlockRelease(block_op_t* block, zx_status_t rc) {
    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    block->cookie = extra->cookie;
    block->completion_cb = extra->completion_cb;
    ReleaseBlock(extra);
    block->completion_cb(block, rc);

    // Try to re-visit any requests we had to defer.
    while (true) {
        switch ((rc = BlockRequeue(&block))) {
        case ZX_ERR_STOP:
        case ZX_ERR_SHOULD_WAIT:
            // Stop processing
            return;
        case ZX_ERR_NEXT:
            ProcessBlock(block);
            break;
        default:
            block->completion_cb(block, rc);
            break;
        }
    }
}

////////////////////////////////////////////////////////////////
// Private methods

zx_status_t Device::AddTaskLocked() {
    if (!active_) {
        zxlogf(ERROR, "device %p is not active\n", this);
        return ZX_ERR_BAD_STATE;
    }
    ++tasks_;

    return ZX_OK;
}

void Device::FinishTaskLocked() {
    --tasks_;
    if (tasks_ == 0) {
        ZX_DEBUG_ASSERT(!active_);
        DdkRemove();
    }
}

zx_status_t Device::BlockAcquire(block_op_t* block) {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);

    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    extra->next = nullptr;
    if (tail_) {
        tail_->next = extra;
        tail_ = extra;
        return ZX_ERR_SHOULD_WAIT;
    }

    if ((rc = BlockAcquireLocked(block->rw.length, extra)) == ZX_ERR_SHOULD_WAIT) {
        head_ = extra;
        tail_ = extra;
    }

    return rc;
}

zx_status_t Device::BlockAcquireLocked(uint64_t len, extra_op_t* extra) {
    zx_status_t rc;

    if ((rc = AddTaskLocked()) != ZX_OK) {
        return rc;
    }
    auto cleanup = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS { FinishTaskLocked(); });

    // Find an available op offset
    uint64_t off;
    if (last_ == 0 || last_ == map_.size()) {
        rc = map_.Find(false, 0, map_.size(), len, &off);
    } else if ((rc = map_.Find(false, last_, map_.size(), len, &off)) == ZX_ERR_NO_RESOURCES) {
        rc = map_.Find(false, 0, last_, len, &off);
    }
    if (rc == ZX_ERR_NO_RESOURCES) {
        return ZX_ERR_SHOULD_WAIT;
    }

    // Reserve space in the map
    if (rc != ZX_OK || (rc = map_.Set(off, off + len)) != ZX_OK) {
        zxlogf(ERROR, "failed to find available op: %s\n", zx_status_get_string(rc));
        return rc;
    }
    last_ = off + len;

    extra->data = base_ + (off * info_->block_size);
    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Device::BlockRequeue(block_op_t** out_block) {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);

    if (!head_) {
        return ZX_ERR_STOP;
    }
    extra_op_t* extra = head_;
    block_op_t* block = ExtraToBlock(extra, info_->op_size);
    if ((rc = BlockAcquireLocked(block->rw.length, extra)) != ZX_OK) {
        return rc;
    }
    if (!extra->next) {
        tail_ = nullptr;
    }
    head_ = extra->next;
    *out_block = block;

    return ZX_ERR_NEXT;
}

void Device::ProcessBlock(block_op_t* block) {
    zx_status_t rc = ZX_OK;
    ZX_DEBUG_ASSERT(block);

    extra_op_t* extra = BlockToExtra(block, info_->op_size);
    extra->vmo = block->rw.vmo;
    extra->length = block->rw.length;
    extra->offset_dev = block->rw.offset_dev;
    extra->offset_vmo = block->rw.offset_vmo;
    extra->completion_cb = block->completion_cb;
    extra->cookie = block->cookie;

    block->rw.vmo = info_->vmo.get();
    if (add_overflow(block->rw.offset_dev, info_->reserved_blocks, &block->rw.offset_dev)) {
        BlockRelease(block, ZX_ERR_OUT_OF_RANGE);
        return;
    }
    block->rw.offset_vmo = (extra->data - base_) / info_->block_size;
    block->completion_cb = BlockComplete;
    block->cookie = this;

    // Set remaining fields and pass along
    zx_port_packet_t packet;
    packet.key = 0;
    packet.type = ZX_PKT_TYPE_USER;
    packet.status = ZX_ERR_NEXT;
    memcpy(packet.user.c8, &block, sizeof(block));
    if ((block->command & BLOCK_OP_MASK) == BLOCK_OP_READ) {
        BlockForward(block);
    } else if ((rc = port_.queue(&packet)) != ZX_OK) {
        BlockRelease(block, rc);
    }
}

void Device::ReleaseBlock(extra_op_t* extra) {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);

    uint64_t off = (extra->data - base_) / info_->block_size;
    uint64_t len = extra->length;
    if ((rc = map_.Clear(off, off + len)) != ZX_OK) {
        zxlogf(ERROR, "warning: could not clear [%zu, %zu]: %s\n", off, off + len,
               zx_status_get_string(rc));
    }
    FinishTaskLocked();
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
