// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/virtio.h>
#include <virtio/block.h>

typedef struct file_state file_state_t;

/* Stores the state of a block device. */
class VirtioBlock : public VirtioDevice {
public:
    static const size_t kSectorSize = 512;

    VirtioBlock(uintptr_t guest_physmem_addr, size_t guest_physmem_size);
    ~VirtioBlock() override = default;

    // Opens a file to use as backing for the block device.
    //
    // Default to opening the file as read-write, but fall back to read-only
    // if that is not possible.
    zx_status_t Init(const char* path);

    // Our config space is read-only.
    zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t HandleQueueNotify(uint16_t queue_sel) override;

    // Block device that returns reads and writes to a file.
    zx_status_t FileBlockDevice();

    // The 'read-only' feature flag.
    bool is_read_only() { return has_device_features(VIRTIO_BLK_F_RO); }
    void set_read_only() { add_device_features(VIRTIO_BLK_F_RO); }

    // The queue used for handling block reauests.
    virtio_queue_t& queue() { return queue_; }

private:
    static zx_status_t QueueHandler(void* addr, uint32_t len, uint16_t flags, uint32_t* used,
                                    void* context);

    zx_status_t FileRequest(file_state_t* state, void* addr, uint32_t len);

    // File descriptor backing the block device.
    int fd_ = 0;
    // Size of file backing the block device.
    uint64_t size_ = 0;
    // Guards access to |fd_|, such as ensuring no other threads modify the
    // file pointer while it is in use by another thread.
    fbl::Mutex file_mutex_;

    // Queue for handling block requests.
    virtio_queue_t queue_;
    // Device configuration fields.
    virtio_blk_config_t config_ = {};
};
