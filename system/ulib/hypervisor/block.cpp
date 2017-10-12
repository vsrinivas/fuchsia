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

zx_status_t VirtioBlock::HandleQueueNotify(uint16_t queue_sel) {
    if (queue_sel != 0)
        return ZX_ERR_INVALID_ARGS;
    return FileBlockDevice();
}

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
    if (fd_ != 0) {
        fprintf(stderr, "Block device has already been initialized.\n");
        return ZX_ERR_BAD_STATE;
    }

    // Open block file. First try to open as read-write but fall back to read
    // only if that fails.
    fd_ = open(path, O_RDWR);
    if (fd_ < 0) {
        fd_ = open(path, O_RDONLY);
        if (fd_ < 0) {
            fprintf(stderr, "Failed to open block file \"%s\"\n", path);
            return ZX_ERR_IO;
        }
        fprintf(stderr, "Unable to open block file \"%s\" read-write. "
                        "Block device will be read-only.\n",
                path);
        set_read_only();
    }
    // Read file size.
    off_t ret = lseek(fd_, 0, SEEK_END);
    if (ret < 0) {
        fprintf(stderr, "Failed to read size of block file \"%s\"\n", path);
        return ZX_ERR_IO;
    }
    size_ = ret;

    config_.capacity = size_ / kSectorSize;

    return ZX_OK;
}

// Multiple data buffers can be chained in the payload of block read/write
// requests. We pass along the offset (from the sector ID defined in the request
// header) so that subsequent requests can seek to the correct block location.
typedef struct file_state {
    VirtioBlock* block;
    off_t off;

    bool has_payload;
    virtio_blk_req_t* blk_req;
    uint8_t status;
} file_state_t;

zx_status_t VirtioBlock::FileRequest(file_state_t* state, void* addr, uint32_t len) {
    virtio_blk_req_t* blk_req = state->blk_req;

    // From VIRTIO Version 1.0: If the VIRTIO_BLK_F_RO feature is set by
    // the device, any write requests will fail.
    if (blk_req->type == VIRTIO_BLK_T_OUT && is_read_only())
        return ZX_ERR_NOT_SUPPORTED;

    // From VIRTIO Version 1.0: A driver MUST set sector to 0 for a
    // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
    // VIRTIO_BLK_T_FLUSH request.
    if (blk_req->type == VIRTIO_BLK_T_FLUSH && blk_req->sector != 0)
        return ZX_ERR_IO_DATA_INTEGRITY;

    fbl::AutoLock lock(&file_mutex_);
    off_t ret;
    if (blk_req->type != VIRTIO_BLK_T_FLUSH) {
        off_t off = blk_req->sector * kSectorSize + state->off;
        state->off += len;
        ret = lseek(fd_, off, SEEK_SET);
        if (ret < 0) {
            return ZX_ERR_IO;
        }
    }

    switch (blk_req->type) {
    case VIRTIO_BLK_T_IN:
        ret = read(fd_, addr, len);
        break;
    case VIRTIO_BLK_T_OUT:
        ret = write(fd_, addr, len);
        break;
    case VIRTIO_BLK_T_FLUSH:
        len = 0;
        ret = fsync(fd_);
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    return ret != len ? ZX_ERR_IO : ZX_OK;
}

/* Map zx_status_t values to their Virtio counterparts. */
static uint8_t to_virtio_status(zx_status_t status) {
    switch (status) {
    case ZX_OK:
        return VIRTIO_BLK_S_OK;
    case ZX_ERR_NOT_SUPPORTED:
        return VIRTIO_BLK_S_UNSUPP;
    default:
        return VIRTIO_BLK_S_IOERR;
    }
}

zx_status_t VirtioBlock::QueueHandler(void* addr, uint32_t len, uint16_t flags, uint32_t* used,
                                      void* context) {
    file_state_t* file_state = (file_state_t*)context;
    VirtioBlock* block = file_state->block;

    // Header.
    if (file_state->blk_req == NULL) {
        if (len != sizeof(*file_state->blk_req))
            return ZX_ERR_INVALID_ARGS;
        file_state->blk_req = static_cast<virtio_blk_req_t*>(addr);
        return ZX_OK;
    }

    // Payload.
    if (flags & VRING_DESC_F_NEXT) {
        file_state->has_payload = true;
        zx_status_t status = block->FileRequest(file_state, addr, len);
        if (status != ZX_OK) {
            file_state->status = to_virtio_status(status);
        } else {
            *used += len;
        }
        return ZX_OK;
    }

    // Status.
    if (len != sizeof(uint8_t))
        return ZX_ERR_INVALID_ARGS;

    // If there was no payload, call the handler function once.
    if (!file_state->has_payload) {
        zx_status_t status = block->FileRequest(file_state, addr, len);
        if (status != ZX_OK)
            file_state->status = to_virtio_status(status);
    }
    uint8_t* status = static_cast<uint8_t*>(addr);
    *status = file_state->status;
    return ZX_OK;
}

zx_status_t VirtioBlock::FileBlockDevice() {
    zx_status_t status;
    do {
        file_state_t state = {
            .block = this,
            .off = 0,
            .has_payload = false,
            .blk_req = NULL,
            .status = VIRTIO_BLK_S_OK,
        };
        status = virtio_queue_handler(&queue_, &VirtioBlock::QueueHandler, &state);
    } while (status == ZX_ERR_NEXT);
    return status;
}
