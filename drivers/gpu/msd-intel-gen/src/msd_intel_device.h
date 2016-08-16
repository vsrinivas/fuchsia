// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DEVICE_H
#define MSD_DEVICE_H

#include "gtt.h"
#include "magma_util/macros.h"
#include "magma_util/platform_device.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include "register_io.h"

class MsdIntelDevice : public msd_device, public Gtt::Owner {
public:
    virtual ~MsdIntelDevice() {}

    // This takes ownership of the connection so that ownership can be
    // transferred across the MSD ABI by the caller
    std::unique_ptr<MsdIntelConnection> Open(msd_client_id client_id);

    uint32_t device_id() { return device_id_; }

    static MsdIntelDevice* cast(msd_device* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdIntelDevice*>(dev);
    }

    bool Init(void* device_handle);

private:
    MsdIntelDevice();

    // GttOwner
    RegisterIo* register_io()
    {
        DASSERT(register_io_);
        return register_io_.get();
    }

    bool ReadGttSize(unsigned int* gtt_size);

    static const uint32_t kMagic = 0x64657669; //"devi"

    uint32_t device_id_{};

    std::unique_ptr<magma::PlatformDevice> platform_device_;
    std::unique_ptr<RegisterIo> register_io_;
    std::unique_ptr<Gtt> gtt_;

    friend class MsdIntelDriver;
    friend class TestMsdIntelDevice;
};

#endif // MSD_DEVICE_H
