// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_DEVICE_H
#define MSD_VSL_DEVICE_H

#include "gpu_features.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "msd.h"
#include "platform_bus_mapper.h"
#include "platform_device.h"

class MsdVslDevice : public msd_device_t {
public:
    // Creates a device for the given |device_handle| and returns ownership.
    static std::unique_ptr<MsdVslDevice> Create(void* device_handle);

    MsdVslDevice() { magic_ = kMagic; }

    virtual ~MsdVslDevice() = default;

    uint32_t device_id() { return device_id_; }

    bool IsIdle();

private:
    bool Init(void* device_handle);
    void HardwareInit();

    bool SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length, uint16_t* prefetch_out);

    magma::RegisterIo* register_io() { return register_io_.get(); }

    magma::PlatformBusMapper* bus_mapper() { return bus_mapper_.get(); }

    static const uint32_t kMagic = 0x64657669; //"devi"

    std::unique_ptr<magma::PlatformDevice> platform_device_;
    std::unique_ptr<magma::RegisterIo> register_io_;
    std::unique_ptr<GpuFeatures> gpu_features_;
    uint32_t device_id_ = 0;
    std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;

    friend class TestMsdVslDevice;
};

#endif // MSD_VSL_DEVICE_H
