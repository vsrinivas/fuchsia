// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <limits.h>
#include <threads.h>

#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/nand.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <lib/zx/vmo.h>
#include <lib/sync/completion.h>
#include <zircon/listnode.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

// Wrapper for nand_info_t. It simplifies initialization of NandDevice.
struct NandParams : public nand_info_t {
    NandParams() : NandParams(0, 0, 0, 0, 0) {}

    NandParams(uint32_t page_size, uint32_t pages_per_block, uint32_t num_blocks, uint32_t ecc_bits,
               uint32_t oob_size)
        : NandParams(nand_info_t {page_size, pages_per_block, num_blocks, ecc_bits, oob_size,
                     NAND_CLASS_FTL, {}}) {}

    NandParams(const nand_info_t& base) {
        // NandParams has no data members.
        *this = *reinterpret_cast<const NandParams*>(&base);
    }

    uint64_t GetSize() const {
        return static_cast<uint64_t>(page_size + oob_size) * NumPages();
    }

    uint32_t NumPages() const {
        return pages_per_block * num_blocks;
    }
};

class NandDevice;
using DeviceType = ddk::Device<NandDevice, ddk::GetSizable, ddk::Unbindable, ddk::Ioctlable>;

// Provides the bulk of the functionality for a ram-backed NAND device.
class NandDevice : public DeviceType, public ddk::NandProtocol<NandDevice> {
  public:
    explicit NandDevice(const NandParams& params, zx_device_t* parent = nullptr);
    ~NandDevice();

    zx_status_t Bind();
    void DdkRelease() { delete this; }

    // Performs the object initialization, returning the required data to create
    // an actual device (to call device_add()). The provided callback will be
    // called when this device must be removed from the system.
    zx_status_t Init(char name[NAME_MAX]);

    // Device protocol implementation.
    zx_off_t DdkGetSize() { return params_.GetSize(); }
    void DdkUnbind();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

    // NAND protocol implementation.
    void Query(nand_info_t* info_out, size_t* nand_op_size_out);
    void Queue(nand_op_t* operation);
    zx_status_t GetFactoryBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                                       uint32_t* num_bad_blocks);

  private:
    void Kill();
    bool AddToList(nand_op_t* operation);
    bool RemoveFromList(nand_op_t** operation);
    int WorkerThread();
    static int WorkerThreadStub(void* arg);
    uint32_t MainDataSize() const { return params_.NumPages() * params_.page_size; }

    // Implementation of the actual commands.
    zx_status_t ReadWriteData(nand_op_t* operation);
    zx_status_t ReadWriteOob(nand_op_t* operation);
    zx_status_t Erase(nand_op_t* operation);

    uintptr_t mapped_addr_ = 0;
    zx::vmo vmo_;

    NandParams params_;

    fbl::Mutex lock_;
    list_node_t txn_list_ TA_GUARDED(lock_) = {};
    bool dead_ TA_GUARDED(lock_) = false;

    bool thread_created_ = false;

    sync_completion_t wake_signal_;
    thrd_t worker_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};
