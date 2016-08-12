// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DEVICE_H
#define MSD_DEVICE_H

#include "magma_util/macros.h"
#include "msd.h"
#include "msd_intel_connection.h"
#include <ddk/device.h>

class MsdIntelDevice : public msd_device {
public:
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

private:
    MsdIntelDevice();

    static const uint32_t kMagic = 0x64657669; //"devi"

    uint32_t device_id_{};

    friend class MsdIntelDriver;
};

#endif // MSD_DEVICE_H
