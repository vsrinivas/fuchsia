// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>

namespace audio {
namespace max98927 {

class MAX98927Device;
using DeviceType = ddk::Device<MAX98927Device, ddk::Unbindable>;

class MAX98927Device : public DeviceType {
public:
    static fbl::unique_ptr<MAX98927Device> Create(zx_device_t* parent);

    MAX98927Device(zx_device_t* parent) : DeviceType(parent) { }
    ~MAX98927Device() { }

    zx_status_t Bind();
    zx_status_t Initialize();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

private:
    // Play a test tone
    void Test();

    // Enable the device
    void Enable();

    // Methods to read/write registers
    uint8_t ReadReg(uint16_t addr);
    void WriteReg(uint16_t addr, uint8_t val);

    // Debug
    void DumpRegs();
};

}  // namespace max98927
}  // namespace audio
