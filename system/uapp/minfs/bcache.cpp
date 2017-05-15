// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/trace.h>

#include <mxalloc/new.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "minfs.h"
#include "minfs-private.h"

namespace minfs {

mx_status_t Bcache::Readblk(uint32_t bno, void* data) {
    off_t off = bno * kMinfsBlockSize;
    trace(IO, "readblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (read(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        error("minfs: cannot read block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t Bcache::Writeblk(uint32_t bno, const void* data) {
    off_t off = bno * kMinfsBlockSize;
    trace(IO, "writeblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd_, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (write(fd_, data, kMinfsBlockSize) != kMinfsBlockSize) {
        error("minfs: cannot write block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

constexpr uint32_t kModeFind = 0;
constexpr uint32_t kModeLoad = 1;
constexpr uint32_t kModeZero = 2;

static const char* modestr(uint32_t mode) {
    switch (mode) {
    case kModeFind: return "FIND";
    case kModeLoad: return "LOAD";
    case kModeZero: return "ZERO";
    default: return "????";
    }
}

mxtl::RefPtr<BlockNode> Bcache::Get(uint32_t bno, uint32_t mode) {
    trace(BCACHE,"bcache_get() bno=%u %s\n", bno, modestr(mode));
    if (bno >= blockmax_) {
        return nullptr;
    }
    mxtl::RefPtr<BlockNode> blk = hash_.find(bno).CopyPointer();
    if (blk != nullptr) {
        // remove from lru
        assert(blk->flags_ & kBlockLRU);
        assert(!(blk->flags_ & kBlockBusy));
        lists_.Erase(blk, kBlockLRU);
        if (mode == kModeZero) {
            blk->flags_ |= kBlockDirty;
            memset(blk->data(), 0, blocksize_);
        }
        goto done;
    }
    if (mode == kModeFind) {
        blk = nullptr;
    } else {
        if ((blk = lists_.PopFront(kBlockFree)) != nullptr) {
            // nothing extra to do
        } else if ((blk = lists_.PopFront(kBlockLRU)) != nullptr) {
            // remove from hash, bno to be reassigned
            hash_.erase(*blk);
        } else {
            panic("bcache: out of blocks\n");
        }
        blk->bno_ = bno;
        hash_.insert(blk);
        assert(hash_.size() <= kMinfsBlockCacheSize);
        if (mode == kModeZero) {
            blk->flags_ |= kBlockDirty;
            memset(blk->data(), 0, blocksize_);
        } else if (Readblk(bno, blk->data()) < 0) {
            panic("bcache: bno %u read error!\n", bno);
        }
    }
done:
    if (blk) {
        lists_.PushBack(blk, kBlockBusy);
        trace(BCACHE, "bcache_get bno=%u %p\n", bno, blk.get());
    }
    return blk;
}

mxtl::RefPtr<BlockNode> Bcache::Get(uint32_t bno) {
    return Get(bno, kModeLoad);
}

void Bcache::Put(mxtl::RefPtr<BlockNode> blk, uint32_t flags) {
    trace(BCACHE, "bcache_put() bno=%u%s\n", blk->bno_, (flags & kBlockDirty) ? " DIRTY" : "");
    assert(blk->flags_ & kBlockBusy);
    // remove from busy list
    lists_.Erase(blk, kBlockBusy);
    if ((flags | blk->flags_) & kBlockDirty) {
        if (Writeblk(blk->bno_, blk->data()) < 0) {
            error("block write error!\n");
        }
        blk->flags_ &= ~kBlockDirty;
    }
    lists_.PushBack(mxtl::move(blk), kBlockLRU);
}

mx_status_t Bcache::Read(uint32_t bno, void* data, uint32_t off, uint32_t len) {
    trace(BCACHE, "bcache_read() bno=%u off=%u len=%u\n", bno, off, len);
    if ((off > blocksize_) || ((blocksize_ - off) < len)) {
        return ERR_INVALID_ARGS;
    }
    mxtl::RefPtr<BlockNode> blk = Get(bno);
    if (blk != nullptr) {
        void* bdata_src = (void*)((uintptr_t)blk->data() + off);
        memcpy(data, bdata_src, len);
        Put(mxtl::move(blk), 0);
        return 0;
    } else {
        return ERR_IO;
    }
}

mx_status_t Bcache::Write(uint32_t bno, const void* data, uint32_t off, uint32_t len) {
    trace(BCACHE, "bcache_write() bno=%u off=%u len=%u\n", bno, off, len);
    if ((off > blocksize_) || ((blocksize_ - off) < len)) {
        return ERR_INVALID_ARGS;
    }
    mxtl::RefPtr<BlockNode> blk = Get(bno);
    if (blk != nullptr) {
        void* bdata_src = (void*)((uintptr_t)blk->data() + off);
        memcpy(bdata_src, data, len);
        Put(mxtl::move(blk), kBlockDirty);
        return 0;
    } else {
        return ERR_IO;
    }
}

int Bcache::Sync() {
    return fsync(fd_);
}

mx_status_t Bcache::Create(Bcache** out, int fd, uint32_t blockmax, uint32_t blocksize,
                           uint32_t num) {
    AllocChecker ac;
    mxtl::unique_ptr<Bcache> bc(new (&ac) Bcache(fd, blockmax, blocksize));
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status;
    while (num > 0) {
        if ((status = BlockNode::Create(bc.get())) != NO_ERROR) {
            return status;
        }
        num--;
    }

#ifdef __Fuchsia__
    mx_handle_t fifo;
    ssize_t r;

    if ((r = ioctl_block_get_fifos(fd, &fifo)) < 0) {
        return static_cast<mx_status_t>(r);
    } else if ((r = ioctl_block_alloc_txn(fd, &bc->txnid_)) < 0) {
        mx_handle_close(fifo);
        return static_cast<mx_status_t>(r);
    } else if ((status = block_fifo_create_client(fifo, &bc->fifo_client_)) != NO_ERROR) {
        ioctl_block_free_txn(fd, &bc->txnid_);
        mx_handle_close(fifo);
        return status;
    }
#endif

    *out = bc.release();
    return NO_ERROR;
}

#ifdef __Fuchsia__
mx_status_t Bcache::AttachVmo(mx_handle_t vmo, vmoid_t* out) {
    mx_handle_t xfer_vmo;
    mx_status_t status = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo);
    if (status != NO_ERROR) {
        return status;
    }
    ssize_t r = ioctl_block_attach_vmo(fd_, &xfer_vmo, out);
    if (r < 0) {
        mx_handle_close(xfer_vmo);
        return static_cast<mx_status_t>(r);
    }
    return NO_ERROR;
}
#endif

int Bcache::Close() {
    return close(fd_);
}

Bcache::Bcache(int fd, uint32_t blockmax, uint32_t blocksize) :
    fd_(fd), blockmax_(blockmax), blocksize_(blocksize) {}

Bcache::~Bcache() {
#ifdef __Fuchsia__
    if (fifo_client_ != nullptr) {
        ioctl_block_free_txn(fd_, &txnid_);
        block_fifo_release_client(fifo_client_);
        ioctl_block_fifo_close(fd_);
    }
    close(fd_);
#endif
}

size_t BcacheLists::SizeAllSlow() const {
    return list_busy_.size_slow() + list_lru_.size_slow() + list_free_.size_slow();
}

void BcacheLists::PushBack(mxtl::RefPtr<BlockNode> blk, uint32_t block_type) {
    assert(SizeAllSlow() < kMinfsBlockCacheSize);
    block_type &= kBlockLLFlags;
    auto ll = GetList(block_type);
    blk->flags_ |= block_type;
    ll->push_back(mxtl::move(blk));
}

mxtl::RefPtr<BlockNode> BcacheLists::PopFront(uint32_t block_type) {
    assert(SizeAllSlow() == kMinfsBlockCacheSize);
    block_type &= kBlockLLFlags;
    auto ll = GetList(block_type);
    auto blk = ll->pop_front();
    if (blk != nullptr)
        blk->flags_ &= ~block_type;
    return blk;
}

mxtl::RefPtr<BlockNode> BcacheLists::Erase(mxtl::RefPtr<BlockNode> blk, uint32_t block_type) {
    assert(SizeAllSlow() == kMinfsBlockCacheSize);
    block_type &= kBlockLLFlags;
    auto ll = GetList(block_type);
    blk->flags_ &= ~block_type;
    auto ptr = ll->erase(*blk);
    assert(ptr != nullptr);
    return ptr;
}

BcacheLists::LinkedList* BcacheLists::GetList(uint32_t block_type) {
    switch (block_type) {
        case kBlockBusy : return &list_busy_;
        case kBlockLRU  : return &list_lru_;
        case kBlockFree : return &list_free_;
    }
    assert(false); // Invalid Block Cache List
    return nullptr;
}

mx_status_t BlockNode::Create(Bcache* bc) {
    AllocChecker ac;
    mxtl::RefPtr<BlockNode> blk = mxtl::AdoptRef(new (&ac) BlockNode());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    blk->data_.reset(static_cast<char*>(malloc(bc->blocksize_)));
    if (blk->data_ == nullptr) {
        return ERR_NO_MEMORY;
    }
    bc->lists_.PushBack(mxtl::move(blk), kBlockFree);
    return NO_ERROR;
}

BlockNode::BlockNode() : flags_(kBlockFree) {}
BlockNode::~BlockNode() {}

#ifndef __Fuchsia__
// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
#endif

} // namespace minfs
