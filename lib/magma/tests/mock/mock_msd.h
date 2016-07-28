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

#ifndef _MOCK_MSD_H_
#define _MOCK_MSD_H_

#include "magma_util/macros.h"
#include "msd.h"

// This class contains default implementations of msd_device functionality
// to override a specific function to contain test logic, inherit from this
// class, override the desired function
class MsdMockDevice : public msd_device {
public:
    MsdMockDevice() { magic_ = kMagic; }
    virtual ~MsdMockDevice() {}

    virtual int32_t Open(msd_client_id client_id) { return 0; }
    virtual int32_t Close(msd_client_id client_id) { return 0; }
    virtual uint32_t GetDeviceId() { return 0; }

    static MsdMockDevice* cast(msd_device* dev)
    {
        DASSERT(dev);
        DASSERT(dev->magic_ == kMagic);
        return static_cast<MsdMockDevice*>(dev);
    }

private:
    static const uint32_t kMagic = 0x6d6b6476; // "mkdv" (Mock Device)
};

class MsdMockDriver : public msd_driver {
public:
    MsdMockDriver() : test_device_(new MsdMockDevice()) { magic_ = kMagic; }
    virtual ~MsdMockDriver() {}

    virtual MsdMockDevice* CreateDevice() { return new MsdMockDevice(); }

    virtual void DestroyDevice(MsdMockDevice* dev) { delete dev; }

    static MsdMockDriver* cast(msd_driver* drv)
    {
        DASSERT(drv);
        DASSERT(drv->magic_ == kMagic);
        return static_cast<MsdMockDriver*>(drv);
    }

private:
    MsdMockDevice* test_device_;
    static const uint32_t kMagic = 0x6d6b6472; // "mkdr" (Mock Driver)
};

#endif