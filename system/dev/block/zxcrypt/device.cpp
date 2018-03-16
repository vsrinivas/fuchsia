// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

#include <ddk/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <lib/zx/port.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zxcrypt/volume.h>

#include "device.h"
#include "extra.h"
#include "worker.h"

#define ZXDEBUG 0

namespace zxcrypt {
namespace {

// TODO(aarongreen): See ZX-1616.  Tune this value.  Possibly break into several smaller VMOs if we
// want to allow some to be recycled; support for this doesn't currently exist. Up to 64 MB may be
// in flight at once.  max_transfer_size will be capped at 1/4 of this value.
const uint64_t kVmoSize = 1UL << 24;
static_assert(kVmoSize % PAGE_SIZE == 0, "kVmoSize must be PAGE_SIZE aligned");

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
        xprintf("zxcrypt device %p initialization aborted: failed to start thread\n", this);
        return ZX_ERR_INTERNAL;
    }

    // Add the (invisible) device to devmgr
    if ((rc = DdkAdd("zxcrypt", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
        xprintf("DdkAdd('zxcrypt', DEVICE_ADD_INVISIBLE) failed: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t Device::Init() {
    zx_status_t rc;

    xprintf("zxcrypt device %p initializing\n", this);
    fbl::AutoLock lock(&mtx_);
    // We make an extra call to |AddTask| to ensure the counter never goes to zero before the
    // corresponding extra call to |FinishTask| in |DdkUnbind|.
    active_ = true;
    AddTaskLocked();

    // Clang gets confused and thinks the thread isn't holding the lock
    auto cleanup = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        xprintf("zxcrypt device %p failed to initialize\n", this);
        lock.release();
        DdkUnbind();
    });

    fbl::AllocChecker ac;
    fbl::unique_ptr<DeviceInfo> info(new (&ac) DeviceInfo);
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(DeviceInfo));
        return ZX_ERR_NO_MEMORY;
    }

    // Open the zxcrypt volume.  The volume may adjust the block info, so get it again and determine
    // the multiplicative factor needed to transform this device's blocks into its parent's.
    // TODO(security): ZX-1130 workaround.  Use null key of a fixed length until fixed
    crypto::Bytes root_key;
    fbl::unique_ptr<Volume> volume;
    if ((rc = root_key.InitZero(kZx1130KeyLen)) != ZX_OK ||
        (rc = Volume::Open(parent(), root_key, 0, &volume)) != ZX_OK ||
        (rc = volume->GetBlockInfo(&info->blk)) != ZX_OK ||
        (rc = volume->GetFvmInfo(&fvm_, &info->has_fvm)) != ZX_OK) {
        return rc;
    }

    // Get the parent device's block interface
    block_info_t blk;
    if ((rc = device_get_protocol(parent(), ZX_PROTOCOL_BLOCK, &info->proto)) != ZX_OK) {
        xprintf("failed to get block protocol: %s\n", zx_status_get_string(rc));
        return rc;
    }
    info->proto.ops->query(info->proto.ctx, &blk, &info->op_size);

    // Save device sizes
    if (info->blk.max_transfer_size == 0 || info->blk.max_transfer_size > kVmoSize / 4) {
        info->blk.max_transfer_size = kVmoSize / 4;
    }
    info->mapped_len = info->blk.block_size * kMaxOps;
    info->offset_dev = Volume::kReservedSlices * (fvm_.slice_size / info->blk.block_size);
    info->op_size += sizeof(extra_op_t);
    info->scale = info->blk.block_size / blk.block_size;

    // Reserve space for shadow I/O transactions
    if ((rc = zx::vmo::create(info->mapped_len, 0, &info->vmo)) != ZX_OK) {
        xprintf("zx::vmo::create failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    if ((rc = zx::vmar::root_self().map(0, info->vmo, 0, info->mapped_len,
                                        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &mapped_)) !=
        ZX_OK) {
        xprintf("zx::vmar::map failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    base_ = reinterpret_cast<uint8_t*>(mapped_);
    if ((rc = map_.Reset(kMaxOps)) != ZX_OK) {
        xprintf("bitmap allocation failed: %s\n", zx_status_get_string(rc));
        return rc;
    }
    // TODO(aarongreen): Investigate performance implications of adding more workers.
    if ((rc = zx::port::create(0, &port_)) != ZX_OK) {
        xprintf("zx::port::create failed: %s\n", zx_status_get_string(rc));
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
    xprintf("zxcrypt device %p initialized\n", this);

    cleanup.cancel();
    return ZX_OK;
}

////////////////////////////////////////////////////////////////
// ddk::Device methods

zx_status_t Device::DdkIoctl(uint32_t op, const void* in, size_t in_len, void* out, size_t out_len,
                             size_t* actual) {
    zx_status_t rc;
    *actual = 0;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        if (!out || out_len < sizeof(info_->blk)) {
            xprintf("bad parameter(s): out=%p, out_len=%zu\n", out, out_len);
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(out, &info_->blk, sizeof(info_->blk));
        *actual = sizeof(info_->blk);
        return ZX_OK;
    }

    case IOCTL_BLOCK_FVM_EXTEND:
    case IOCTL_BLOCK_FVM_SHRINK: {
        if (!info_->has_fvm) {
            xprintf("FVM ioctl to non-FVM device\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (!in || in_len < sizeof(extend_request_t)) {
            xprintf("bad parameter(s): in=%p, in_len=%zu\n", in, in_len);
            return ZX_ERR_INVALID_ARGS;
        }
        // Skip the leading reserved slice, and fail if it would touch the trailing reserved slice.
        extend_request_t mod;
        memcpy(&mod, in, sizeof(mod));
        mod.offset += Volume::kReservedSlices;
        // Send the actual ioctl
        if ((rc = device_ioctl(parent(), op, &mod, sizeof(mod), out, out_len, actual)) < 0) {
            return rc;
        }
        if (op == IOCTL_BLOCK_FVM_EXTEND) {
            fbl::AutoLock lock(&mtx_);
            fvm_.vslice_count += mod.length;
        } else {
            fbl::AutoLock lock(&mtx_);
            fvm_.vslice_count -= mod.length;
        }
        return ZX_OK;
    }

    case IOCTL_BLOCK_FVM_QUERY: {
        if (!info_->has_fvm) {
            xprintf("FVM ioctl to non-FVM device\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (!out || out_len < sizeof(fvm_)) {
            xprintf("bad parameter(s): out=%p, out_len=%zu\n", out, out_len);
            return ZX_ERR_INVALID_ARGS;
        }
        // FVM info has an already adjusted vslice_count
        fbl::AutoLock lock(&mtx_);
        memcpy(out, &fvm_, sizeof(fvm_));
        *actual = sizeof(fvm_);
        return ZX_OK;
    }

    case IOCTL_BLOCK_FVM_VSLICE_QUERY: {
        if (!info_->has_fvm) {
            xprintf("FVM ioctl to non-FVM device\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (!in || in_len != sizeof(query_request_t)) {
            xprintf("bad parameter(s): in=%p, in_len=%zu\n", in, in_len);
            return ZX_ERR_INVALID_ARGS;
        }
        // Shift requested offsets to skip the leading reserved block.
        const query_request_t* original = static_cast<const query_request_t*>(in);
        query_request_t mod;
        mod.count = original->count;
        for (size_t i = 0; i < mod.count; ++i) {
            mod.vslice_start[i] = original->vslice_start[i] + Volume::kReservedSlices;
        }
        in = &mod;
        // fall-through
    }

    default:
        // Pass-through to parent
        return device_ioctl(parent(), op, in, in_len, out, out_len, actual);
    }
}

zx_off_t Device::DdkGetSize() {
    return info_->blk.block_count * info_->blk.block_size;
}

// TODO(aarongreen): See ZX-1138.  Currently, there's no good way to trigger
// this on demand.
void Device::DdkUnbind() {
    xprintf("zxcrypt device %p unbinding\n", this);
    fbl::AutoLock lock(&mtx_);
    active_ = false;
    zx_port_packet_t packet;
    packet.key = 0;
    packet.type = ZX_PKT_TYPE_USER;
    packet.status = ZX_ERR_STOP;
    for (size_t i = 0; i < kNumWorkers; ++i) {
        port_.queue(&packet, 1);
    }
    port_.reset();
    // See |Init|; this is the "extra" call to |FinishTask|.
    FinishTaskLocked();
}

void Device::DdkRelease() {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);
    thrd_join(init_, &rc);
    if (rc != ZX_OK) {
        xprintf("WARNING: init thread returned %s\n", zx_status_get_string(rc));
    }
    for (size_t i = 0; i < kNumWorkers; ++i) {

        workers_[i].Stop();
    }
    if (mapped_ != 0 && (rc = zx::vmar::root_self().unmap(mapped_, info_->mapped_len)) != ZX_OK) {
        xprintf("WARNING: failed to unmap %zu bytes at %" PRIuPTR ": %s\n", info_->mapped_len,
                mapped_, zx_status_get_string(rc));
    }
    fbl::unique_ptr<DeviceInfo> info(const_cast<DeviceInfo*>(info_));
    info_ = nullptr;
    xprintf("zxcrypt device %p released\n", this);
    lock.release();
    delete this;
}

////////////////////////////////////////////////////////////////
// ddk::BlockProtocol methods

void Device::BlockQuery(block_info_t* out_info, size_t* out_op_size) {
    fbl::AutoLock lock(&mtx_);
    // Copy requested data
    if (out_info) {
        memcpy(out_info, &info_->blk, sizeof(info_->blk));
    }
    if (out_op_size) {
        *out_op_size = info_->op_size;
    }
}

void Device::BlockQueue(block_op_t* block) {
    zx_status_t rc;
    ZX_DEBUG_ASSERT(info_->proto.ctx);

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
    // Must start in range; must not overflow
    uint64_t end;
    if (add_overflow(block->rw.offset_dev, block->rw.length, &end)) {
        xprintf("overflow: off=%" PRIu64 ", len=%" PRIu32 "\n", block->rw.offset_dev,
                block->rw.length);
        block->completion_cb(block, ZX_ERR_INVALID_ARGS);
        return;
    }
    if (block->rw.offset_dev >= info_->blk.block_count ||
        info_->blk.block_count < end) {
        xprintf("[%" PRIu64 ", %" PRIu64 "] is not wholly within device\n", block->rw.offset_dev,
                end);
        block->completion_cb(block, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    // Reserve space to do cryptographic transformations
    uint64_t off;
    rc = AcquireBlocks(block->rw.length, &off);
    switch (rc) {
    case ZX_OK:
        ProcessBlock(block, off);
        break;
    case ZX_ERR_NO_RESOURCES:
        EnqueueBlock(block);
        break;
    default:
        block->completion_cb(block, rc);
    }
}

void Device::BlockForward(block_op_t* block) {
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
    if ((rc = device->port_.queue(&packet, 1)) != ZX_OK) {
        device->BlockRelease(block, rc);
    }
}

void Device::BlockRelease(block_op_t* block, zx_status_t rc) {
    uint64_t off = block->rw.offset_vmo / info_->scale;
    uint64_t len = block->rw.length / info_->scale;

    extra_op_t* extra = BlockToExtra(block);
    block->cookie = extra->cookie;
    extra->completion_cb(block, rc);
    ReleaseBlocks(off, len);

    // Try to re-visit any requests we had to defer.
    while ((block = DequeueBlock())) {
        rc = AcquireBlocks(block->rw.length, &off);
        switch (rc) {
        case ZX_OK:
            ProcessBlock(block, off);
            break;
        case ZX_ERR_NO_RESOURCES:
            RequeueBlock(block);
            break;
        default:
            block->completion_cb(block, rc);
        }
    }
}

extra_op_t* Device::BlockToExtra(block_op_t* block) const {
    ZX_DEBUG_ASSERT(block);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(block);
    return reinterpret_cast<extra_op_t*>(ptr + info_->op_size) - 1;
}

block_op_t* Device::ExtraToBlock(extra_op_t* extra) const {
    ZX_DEBUG_ASSERT(extra);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(extra + 1);
    return reinterpret_cast<block_op_t*>(ptr - info_->op_size);
}

////////////////////////////////////////////////////////////////
// Private methods

zx_status_t Device::AddTaskLocked() {
    if (!active_) {
        xprintf("device %p is not active\n", this);
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

zx_status_t Device::AcquireBlocks(uint64_t len, uint64_t* out) {
    fbl::AutoLock lock(&mtx_);
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

    // Reserve space in the map
    if (rc != ZX_OK || (rc = map_.Set(off, off + len)) != ZX_OK) {
        xprintf("failed to find available op: %s\n", zx_status_get_string(rc));
        return rc;
    }
    last_ = off + len;

    *out = off;
    cleanup.cancel();
    return ZX_OK;
}

void Device::ReleaseBlocks(uint64_t off, uint64_t len) {
    zx_status_t rc;
    fbl::AutoLock lock(&mtx_);

    if ((rc = map_.Clear(off, off + len)) != ZX_OK) {
        xprintf("warning: could not clear [%zu, %zu]: %s\n", off, off + len,
                zx_status_get_string(rc));
    }
    FinishTaskLocked();
}

void Device::ProcessBlock(block_op_t* block, uint64_t off) {
    zx_status_t rc;

    extra_op_t* extra = BlockToExtra(block);
    extra->buf = base_ + (off * info_->blk.block_size);
    extra->len = block->rw.length * info_->blk.block_size;
    extra->num = block->rw.offset_dev * info_->blk.block_size;
    extra->off = block->rw.offset_vmo * info_->blk.block_size;
    extra->vmo = block->rw.vmo;
    extra->completion_cb = block->completion_cb;
    extra->cookie = block->cookie;

    block->rw.vmo = info_->vmo.get();
    block->rw.length *= info_->scale;
    block->rw.offset_dev = (block->rw.offset_dev + info_->offset_dev) * info_->scale;
    block->rw.offset_vmo = off * info_->scale;
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
    } else if ((rc = port_.queue(&packet, 1)) != ZX_OK) {
        BlockRelease(block, rc);
    }
}

void Device::EnqueueBlock(block_op_t* block) {
    fbl::AutoLock lock(&mtx_);
    extra_op_t* extra = BlockToExtra(block);
    extra->next = nullptr;
    if (tail_) {
        tail_->next = extra;
    } else {
        head_ = extra;
    }
    tail_ = extra;
}

block_op_t* Device::DequeueBlock() {
    fbl::AutoLock lock(&mtx_);

    if (!head_) {
        return nullptr;
    }
    extra_op_t* extra = head_;
    if (!extra->next) {
        tail_ = nullptr;
    }
    head_ = extra->next;
    return ExtraToBlock(extra);
}

void Device::RequeueBlock(block_op_t* block) {
    fbl::AutoLock lock(&mtx_);
    extra_op_t* extra = BlockToExtra(block);
    extra->next = head_;
    head_ = extra;
    if (!tail_) {
        tail_ = extra;
    }
}

} // namespace zxcrypt

extern "C" zx_status_t zxcrypt_device_bind(void* ctx, zx_device_t* parent) {
    zx_status_t rc;
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<zxcrypt::Device>(&ac, parent);
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(zxcrypt::Device));
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
