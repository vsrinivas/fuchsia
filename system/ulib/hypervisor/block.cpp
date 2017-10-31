// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/block.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <block-client/client.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/device/block.h>

// Dispatcher that fulfills block requests using file-descriptor IO
// (ex: read/write to a file descriptor).
class FdioBlockDispatcher : public VirtioBlockRequestDispatcher {
public:
    static zx_status_t Create(int fd, fbl::unique_ptr<VirtioBlockRequestDispatcher>* out) {
        fbl::AllocChecker ac;
        auto dispatcher = fbl::make_unique_checked<FdioBlockDispatcher>(&ac, fd);
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;

        *out = fbl::move(dispatcher);
        return ZX_OK;
    }

    FdioBlockDispatcher(int fd)
        : fd_(fd) {}

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

class FifoBlockDispatcher : public VirtioBlockRequestDispatcher {
public:
    static zx_status_t Create(int fd, const PhysMem& phys_mem,
                              fbl::unique_ptr<VirtioBlockRequestDispatcher>* out) {
        zx_handle_t fifo;
        ssize_t result = ioctl_block_get_fifos(fd, &fifo);
        if (result != sizeof(fifo))
            return ZX_ERR_IO;
        auto close_fifo = fbl::MakeAutoCall([fifo]() { zx_handle_close(fifo); });

        txnid_t txnid = TXNID_INVALID;
        result = ioctl_block_alloc_txn(fd, &txnid);
        if (result != sizeof(txnid_))
            return ZX_ERR_IO;
        auto free_txn = fbl::MakeAutoCall([fd, txnid]() { ioctl_block_free_txn(fd, &txnid); });

        zx_handle_t vmo_dup;
        zx_status_t status = zx_handle_duplicate(phys_mem.vmo(), ZX_RIGHT_SAME_RIGHTS, &vmo_dup);
        if (status != ZX_OK)
            return ZX_ERR_IO;

        // TODO(ZX-1333): Limit how much of they guest physical address space
        // is exposed to the block server.
        vmoid_t vmoid;
        result = ioctl_block_attach_vmo(fd, &vmo_dup, &vmoid);
        if (result != sizeof(vmoid_)) {
            zx_handle_close(vmo_dup);
            return ZX_ERR_IO;
        }

        fifo_client_t* fifo_client = nullptr;
        status = block_fifo_create_client(fifo, &fifo_client);
        if (status != ZX_OK)
            return ZX_ERR_IO;

        // The fifo handle is now owned by the block client.
        fifo = ZX_HANDLE_INVALID;
        auto free_fifo_client = fbl::MakeAutoCall(
            [fifo_client]() { block_fifo_release_client(fifo_client); });

        fbl::AllocChecker ac;
        auto dispatcher = fbl::make_unique_checked<FifoBlockDispatcher>(&ac, fd, txnid, vmoid,
                                                                        fifo_client,
                                                                        phys_mem.addr());
        if (!ac.check())
            return ZX_ERR_NO_MEMORY;

        close_fifo.cancel();
        free_txn.cancel();
        free_fifo_client.cancel();
        *out = fbl::move(dispatcher);
        return ZX_OK;
    }

    FifoBlockDispatcher(int fd, txnid_t txnid, vmoid_t vmoid, fifo_client_t* fifo_client,
                        size_t guest_vmo_addr)
        : fd_(fd), txnid_(txnid), vmoid_(vmoid), fifo_client_(fifo_client),
          guest_vmo_addr_(guest_vmo_addr) {}

    ~FifoBlockDispatcher() {
        if (txnid_ != TXNID_INVALID) {
            ioctl_block_free_txn(fd_, &txnid_);
        }
        if (fifo_client_ != nullptr) {
            block_fifo_release_client(fifo_client_);
        }
    }

    zx_status_t Flush() override {
        return ZX_OK;
    }

    zx_status_t Read(off_t disk_offset, void* buf, size_t size) override {
        fbl::AutoLock lock(&fifo_mutex_);
        return EnqueueBlockRequestLocked(BLOCKIO_READ, disk_offset, buf, size);
    }

    zx_status_t Write(off_t disk_offset, const void* buf, size_t size) override {
        fbl::AutoLock lock(&fifo_mutex_);
        return EnqueueBlockRequestLocked(BLOCKIO_WRITE, disk_offset, buf, size);
    }

    zx_status_t Submit() override {
        fbl::AutoLock lock(&fifo_mutex_);
        return SubmitTransactionsLocked();
    }

private:
    zx_status_t EnqueueBlockRequestLocked(uint16_t opcode, off_t disk_offset, const void* buf,
                                          size_t size) TA_REQ(fifo_mutex_) {
        if (request_index_ >= kNumRequests) {
            zx_status_t status = SubmitTransactionsLocked();
            if (status != ZX_OK)
                return status;
        }

        block_fifo_request_t* request = &requests_[request_index_++];
        request->txnid = txnid_;
        request->vmoid = vmoid_;
        request->opcode = opcode;
        request->length = size;
        request->vmo_offset = reinterpret_cast<uint64_t>(buf) - guest_vmo_addr_;
        request->dev_offset = disk_offset;
        return ZX_OK;
    }

    zx_status_t SubmitTransactionsLocked() TA_REQ(fifo_mutex_) {
        zx_status_t status = block_fifo_txn(fifo_client_, requests_, request_index_);
        request_index_ = 0;
        return status;
    }

    // Block server access.
    int fd_;
    txnid_t txnid_ = TXNID_INVALID;
    vmoid_t vmoid_;
    fifo_client_t* fifo_client_ = nullptr;

    size_t guest_vmo_addr_;
    size_t request_index_ TA_GUARDED(fifo_mutex_) = 0;
    static constexpr size_t kNumRequests = MAX_TXN_MESSAGES;
    block_fifo_request_t requests_[kNumRequests] TA_GUARDED(fifo_mutex_);
    fbl::Mutex fifo_mutex_;
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

zx_status_t VirtioBlock::Init(const char* path, const PhysMem& phys_mem) {
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

    // Prefer using the faster FIFO-based IO. If the file is not a block device
    // file then fall back to using posix IO.
    fbl::unique_ptr<VirtioBlockRequestDispatcher> dispatcher;
    zx_status_t status = FifoBlockDispatcher::Create(fd, phys_mem, &dispatcher);
    if (status == ZX_OK) {
        printf("virtio-block: Using FIFO IO for block device '%s'.\n", path);
    } else {
        status = FdioBlockDispatcher::Create(fd, &dispatcher);
        if (status != ZX_OK)
            return status;
        printf("virtio-block: Using posix IO for block device '%s'.\n", path);
    }
    dispatcher_ = fbl::move(dispatcher);

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
