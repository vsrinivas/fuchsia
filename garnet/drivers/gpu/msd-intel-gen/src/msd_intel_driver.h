// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_DRIVER_H
#define MSD_DRIVER_H

#include "magma_util/macros.h"
#include "msd.h"
#include <memory>

class MsdIntelDriver : public msd_driver_t {
public:
    virtual ~MsdIntelDriver() {}

    static std::unique_ptr<MsdIntelDriver> Create();
    static void Destroy(MsdIntelDriver* drv);

    static MsdIntelDriver* cast(msd_driver_t* drv)
    {
        DASSERT(drv);
        DASSERT(drv->magic_ == kMagic);
        return static_cast<MsdIntelDriver*>(drv);
    }

    void configure(uint32_t flags) { configure_flags_ = flags; }

    uint32_t configure_flags() { return configure_flags_; }

private:
    MsdIntelDriver();

    static const uint32_t kMagic = 0x64726976; //"driv"

    uint32_t configure_flags_ = 0;
};

#endif // MSD_DRIVER_H
