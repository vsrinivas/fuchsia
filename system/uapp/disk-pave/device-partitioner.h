// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <gpt/gpt.h>
#include <zircon/types.h>

namespace paver {

enum class Partition {
    kKernelC,
    kEfi,
    kFuchsiaVolumeManager,
    // The following are only valid for WipePartition.
    kInstallType,
    kSystem,
    kBlob,
    kData,
};

// Abstract device partitioner definition.
// This class defines common APIs for interacting with a device partitioner.
class DevicePartitioner {
public:
    // Factory method which automatically returns the correct DevicePartitioner
    // implementation. Returns nullptr on failure.
    static fbl::unique_ptr<DevicePartitioner> Create();

    virtual ~DevicePartitioner() = default;

    virtual bool IsCros() const = 0;

    // Returns a file descriptor to a partition of type |partition_type|, creating it.
    // Assumes that the partition does not already exist.
    virtual zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) = 0;

    // Returns a file descriptor to a partition of type |partition_type| if one exists.
    virtual zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const = 0;

    // Finalizes the partition of type |partition_type| after it has been
    // written.
    virtual zx_status_t FinalizePartition(Partition partition_type) = 0;

    // Wipes partition list specified.
    virtual zx_status_t WipePartitions(const fbl::Vector<Partition>& partitions) = 0;

    // Returns block info for specified block.
    virtual zx_status_t GetBlockInfo(const fbl::unique_fd& block_fd,
                                     block_info_t* block_info) const = 0;
};

// Useful for when a GPT table is available (e.g. x86 devices). Provides common
// utility functions.
class GptDevicePartitioner {
public:
    using FilterCallback = fbl::Function<bool(const gpt_partition_t&)>;

    // Find and initialize a GPT based device.
    static zx_status_t InitializeGpt(fbl::unique_ptr<GptDevicePartitioner>* gpt_out);

    virtual ~GptDevicePartitioner() {
        if (gpt_) {
            gpt_device_release(gpt_);
        }
    }

    // Returns block info for a specified block device.
    zx_status_t GetBlockInfo(block_info_t* block_info) const {
        memcpy(block_info, &block_info_, sizeof(*block_info));
        return ZX_OK;
    }

    gpt_device_t* GetGpt() const { return gpt_; }
    int GetFd() const { return fd_.get(); }

    // Find the first spot that has at least |bytes_requested| of space.
    //
    // Returns the |start_out| block and |length_out| blocks, indicating
    // how much space was found, on success. This may be larger than
    // the number of bytes requested.
    zx_status_t FindFirstFit(size_t bytes_requested, size_t* start_out, size_t* length_out) const;

    // Creates a partition, adds an entry to the GPT, and returns a file descriptor to it.
    // Assumes that the partition does not already exist.
    zx_status_t AddPartition(const char* name, uint8_t* type, size_t minimum_size_bytes,
                             size_t optional_reserve_bytes, fbl::unique_fd* out_fd);

    // Returns a file descriptor to a partition which can be paved,
    // if one exists.
    zx_status_t FindPartition(FilterCallback filter, gpt_partition_t** out,
                              fbl::unique_fd* out_fd);
    zx_status_t FindPartition(FilterCallback filter, fbl::unique_fd* out_fd) const;

    // Wipes a specified partition from the GPT, and ovewrites first 8KiB with
    // nonsense.
    zx_status_t WipePartitions(FilterCallback filter);

private:
    // Find and return the topological path of the GPT which we will pave.
    static bool FindTargetGptPath(fbl::String* out);

    GptDevicePartitioner(fbl::unique_fd fd, gpt_device_t* gpt, block_info_t block_info)
        : fd_(fbl::move(fd)), gpt_(gpt), block_info_(block_info) {}

    zx_status_t CreateGptPartition(const char* name, uint8_t* type, uint64_t offset,
                                   uint64_t blocks, uint8_t* out_guid);

    fbl::unique_fd fd_;
    gpt_device_t* gpt_;
    block_info_t block_info_;
};

// DevicePartitioner implementation for EFI based devices.
class EfiDevicePartitioner : public DevicePartitioner {
public:
    static zx_status_t Initialize(fbl::unique_ptr<DevicePartitioner>* partitioner);

    bool IsCros() const override { return false; }

    zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) override;

    zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

    zx_status_t FinalizePartition(Partition unused) override { return ZX_OK; }

    zx_status_t WipePartitions(const fbl::Vector<Partition>& partitions) override;

    zx_status_t GetBlockInfo(const fbl::unique_fd& block_fd,
                             block_info_t* block_info) const override {
        return gpt_->GetBlockInfo(block_info);
    }

private:
    EfiDevicePartitioner(fbl::unique_ptr<GptDevicePartitioner> gpt)
        : gpt_(fbl::move(gpt)) {}

    static bool FilterZirconPartition(const block_info_t& info, const gpt_partition_t& part);

    fbl::unique_ptr<GptDevicePartitioner> gpt_;
};

// DevicePartitioner implementation for ChromeOS devices.
class CrosDevicePartitioner : public DevicePartitioner {
public:
    static zx_status_t Initialize(fbl::unique_ptr<DevicePartitioner>* partitioner);

    bool IsCros() const override { return true; }

    zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) override;

    zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

    zx_status_t FinalizePartition(Partition unused) override;

    zx_status_t WipePartitions(const fbl::Vector<Partition>& partitions) override;

    zx_status_t GetBlockInfo(const fbl::unique_fd& block_fd,
                             block_info_t* block_info) const override {
        return gpt_->GetBlockInfo(block_info);
    }

private:
    CrosDevicePartitioner(fbl::unique_ptr<GptDevicePartitioner> gpt)
        : gpt_(fbl::move(gpt)) {}

    fbl::unique_ptr<GptDevicePartitioner> gpt_;
};

} // namespace paver
