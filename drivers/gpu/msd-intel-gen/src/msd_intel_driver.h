// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DRIVER_H
#define MSD_DRIVER_H

#include "magma_util/macros.h"
#include "msd.h"

class MsdIntelDevice;

class MsdIntelDriver : public msd_driver {
public:
    MsdIntelDevice* CreateDevice(void* device);

    static MsdIntelDriver* Create();
    static void Destroy(MsdIntelDriver* drv);

    static MsdIntelDriver* cast(msd_driver* drv)
    {
        DASSERT(drv);
        DASSERT(drv->magic_ == kMagic);
        return static_cast<MsdIntelDriver*>(drv);
    }

private:
    MsdIntelDriver();
    virtual ~MsdIntelDriver() {}

    static const uint32_t kMagic = 0x64726976; //"driv"
};

#endif // MSD_DRIVER_H
