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

#ifndef _INTEL_GEN_H_
#define _INTEL_GEN_H_

#include <ddk/device.h>
#include <magma_sys_driver.h>

struct MsdDevice {
public:
    MsdDevice(mx_device_t* mx_device) {}

    virtual ~MsdDevice() {}
};

struct MagmaSysDriver {
public:
    virtual ~MagmaSysDriver() {}

    virtual MsdDevice* CreateDevice(void* device) = 0;
    virtual void DestroyDevice(MsdDevice*) = 0;

    static MagmaSysDriver* New();
    static void Delete(MagmaSysDriver* gen);
};

#endif // _INTEL_GEN_H_
