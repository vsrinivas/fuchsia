// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/virtio.h>
#include <virtio/block.h>

typedef struct file_state file_state_t;

// Component to service block requests.
class VirtioBlockRequestDispatcher {
public:
    virtual ~VirtioBlockRequestDispatcher() = default;

    virtual zx_status_t Flush() = 0;
    virtual zx_status_t Read(off_t disk_offset, void* buf, size_t size) = 0;
    virtual zx_status_t Write(off_t disk_offset, const void* buf, size_t size) = 0;
    virtual zx_status_t Submit() = 0;

};

// Stores the state of a block device.
class VirtioBlock : public VirtioDevice {
public:
    static const size_t kSectorSize = 512;

    VirtioBlock(uintptr_t guest_physmem_addr, size_t guest_physmem_size);
    ~VirtioBlock() override = default;

    // Opens a file to use as backing for the block device.
    //
    // Default to opening the file as read-write, but fall back to read-only
    // if that is not possible.
    zx_status_t Init(const char* path, const PhysMem& phys_mem);

    // Starts a thread to monitor the queue for incomming block requests.
    zx_status_t Start();

    // Our config space is read-only.
    zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t HandleBlockRequest(virtio_queue_t* queue, uint16_t head, uint32_t* used);

    // The 'read-only' feature flag.
    bool is_read_only() { return has_device_features(VIRTIO_BLK_F_RO); }
    void set_read_only() { add_device_features(VIRTIO_BLK_F_RO); }

    // The queue used for handling block reauests.
    virtio_queue_t& queue() { return queue_; }

private:
    // Size of file backing the block device.
    uint64_t size_ = 0;
    // Queue for handling block requests.
    virtio_queue_t queue_;
    // Device configuration fields.
    virtio_blk_config_t config_ = {};

    fbl::unique_ptr<VirtioBlockRequestDispatcher> dispatcher_;
};
