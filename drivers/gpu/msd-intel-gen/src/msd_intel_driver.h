// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
