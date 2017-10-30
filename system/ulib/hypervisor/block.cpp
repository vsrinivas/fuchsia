// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/block.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>

// Dispatcher that fulfills block requests using file-descriptor IO
// (ex: read/write to a file descriptor).
class FdioBlockDispatcher : public VirtioBlockRequestDispatcher {
public:
    FdioBlockDispatcher(int fd): fd_(fd) {}

    zx_status_t Flush() override {
        fbl::AutoLock lock(&file_mutex_);
        return fsync(fd_) == 0 ? ZX_OK : ZX_ERR_IO;
    }

    zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
        fbl::AutoLock lock(&file_mutex_);
        off_t off = lseek(fd_, disk_offset, SEEK_SET);
        if (off < 0)
            return ZX_ERR_IO;

        size_t ret = read(fd_, buf, size);
        if (ret != size)
            return ZX_ERR_IO;
        return ZX_OK;
    }

    zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
        fbl::AutoLock lock(&file_mutex_);
        off_t off = lseek(fd_, disk_offset, SEEK_SET);
        if (off < 0)
            return ZX_ERR_IO;

        size_t ret = write(fd_, buf, size);
        if (ret != size)
            return ZX_ERR_IO;
        return ZX_OK;
    }

    zx_status_t Submit() override {
        // No-op, all IO methods are synchronous.
        return ZX_OK;
    }

private:
    fbl::Mutex file_mutex_;
    int fd_;
};

VirtioBlock::VirtioBlock(uintptr_t guest_physmem_addr, size_t guest_physmem_size)
    : VirtioDevice(VIRTIO_ID_BLOCK, &config_, sizeof(config_), &queue_, 1,
                   guest_physmem_addr, guest_physmem_size) {
    config_.blk_size = kSectorSize;
    // Virtio 1.0: 5.2.5.2: Devices SHOULD always offer VIRTIO_BLK_F_FLUSH
    add_device_features(VIRTIO_BLK_F_FLUSH
                        // Required by zircon guests.
                        | VIRTIO_BLK_F_BLK_SIZE);
}

zx_status_t VirtioBlock::Init(const char* path) {
    if (dispatcher_ != nullptr) {
        fprintf(stderr, "Block device has already been initialized.\n");
        return ZX_ERR_BAD_STATE;
    }

    // Open block file. First try to open as read-write but fall back to read
    // only if that fails.
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Failed to open block file \"%s\"\n", path);
            return ZX_ERR_IO;
        }
        fprintf(stderr, "Unable to open block file \"%s\" read-write. "
                        "Block device will be read-only.\n",
                path);
        set_read_only();
    }
    // Read file size.
    off_t ret = lseek(fd, 0, SEEK_END);
    if (ret < 0) {
        fprintf(stderr, "Failed to read size of block file \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    size_ = ret;
    config_.capacity = size_ / kSectorSize;

    fbl::AllocChecker ac;
    dispatcher_ = fbl::make_unique_checked<FdioBlockDispatcher>(&ac, fd);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    return ZX_OK;
}

zx_status_t VirtioBlock::Start() {
    auto poll_func = +[](virtio_queue_t* queue, uint16_t head, uint32_t* used, void* ctx) {
        return static_cast<VirtioBlock*>(ctx)->HandleBlockRequest(queue, head, used);
    };
    return virtio_queue_poll(&queue_, poll_func, this);
}

zx_status_t VirtioBlock::HandleBlockRequest(virtio_queue_t* queue, uint16_t head, uint32_t* used) {
    uint8_t block_status = VIRTIO_BLK_S_OK;
    uint8_t* block_status_ptr = nullptr;
    const virtio_blk_req_t* req = nullptr;
    off_t offset = 0;
    virtio_desc_t desc;

    zx_status_t status = virtio_queue_read_desc(queue, head, &desc);
    if (status != ZX_OK) {
        desc.addr = nullptr;
        desc.len = 0;
        desc.has_next = false;
    }

    if (desc.len == sizeof(virtio_blk_req_t)) {
        req = static_cast<const virtio_blk_req_t*>(desc.addr);
    } else {
        block_status = VIRTIO_BLK_S_IOERR;
    }

    // VIRTIO 1.0 Section 5.2.6.2: A device MUST set the status byte to
    // VIRTIO_BLK_S_IOERR for a write request if the VIRTIO_BLK_F_RO feature
    // if offered, and MUST NOT write any data.
    if (req != nullptr && req->type == VIRTIO_BLK_T_OUT && is_read_only()) {
        block_status = VIRTIO_BLK_S_IOERR;
    }

    // VIRTIO Version 1.0: A driver MUST set sector to 0 for a
    // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
    // VIRTIO_BLK_T_FLUSH request.
    if (req != nullptr && req->type == VIRTIO_BLK_T_FLUSH && req->sector != 0) {
        block_status = VIRTIO_BLK_S_IOERR;
    }

    // VIRTIO 1.0 Section 5.2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is
    // negotiated, blk_size can be read to determine the optimal sector size
    // for the driver to use. This does not affect the units used in the
    // protocol (always 512 bytes), but awareness of the correct value can
    // affect performance.
    if (req != nullptr)
        offset = req->sector * kSectorSize;

    while (desc.has_next) {
        status = virtio_queue_read_desc(queue, desc.next, &desc);
        if (status != ZX_OK) {
            block_status = block_status != VIRTIO_BLK_S_OK ? block_status : VIRTIO_BLK_S_IOERR;
            break;
        }

        // Requests should end with a single 1b status byte.
        if (desc.len == 1 && desc.writable && !desc.has_next) {
            block_status_ptr = static_cast<uint8_t*>(desc.addr);
            break;
        }

        // Skip doing any file ops if we've already encountered an error, but
        // keep traversing the descriptor chain looking for the status tailer.
        if (block_status != VIRTIO_BLK_S_OK)
            continue;

        zx_status_t status;
        switch (req->type) {
        case VIRTIO_BLK_T_IN:
            if (desc.len % kSectorSize != 0) {
                block_status = VIRTIO_BLK_S_IOERR;
                continue;
            }
            status = dispatcher_->Read(offset, desc.addr, desc.len);
            *used += desc.len;
            offset += desc.len;
            break;
        case VIRTIO_BLK_T_OUT: {
            if (desc.len % kSectorSize != 0) {
                block_status = VIRTIO_BLK_S_IOERR;
                continue;
            }
            status = dispatcher_->Write(offset, desc.addr, desc.len);
            offset += desc.len;
            break;
        }
        case VIRTIO_BLK_T_FLUSH:
            status = dispatcher_->Flush();
            break;
        default:
            block_status = VIRTIO_BLK_S_UNSUPP;
            break;
        }

        // Report any failures queuing the IO request.
        if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK)
            block_status = VIRTIO_BLK_S_IOERR;
    }

    // Wait for operations to become consistent.
    status = dispatcher_->Submit();
    if (block_status == VIRTIO_BLK_S_OK && status != ZX_OK)
        block_status = VIRTIO_BLK_S_IOERR;

    // Set the output status if we found the byte in the descriptor chain.
    if (block_status_ptr != nullptr) {
        *block_status_ptr = block_status;
        ++*used;
    }
    return ZX_OK;
}
