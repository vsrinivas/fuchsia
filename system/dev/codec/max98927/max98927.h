// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/audio-codec.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>

namespace audio {
namespace max98927 {

class Max98927Device;
using DeviceType = ddk::Device<Max98927Device, ddk::Ioctlable, ddk::Unbindable>;

class Max98927Device : public DeviceType,
                       public ddk::AudioCodecProtocol<Max98927Device> {
public:
    static fbl::unique_ptr<Max98927Device> Create(zx_device_t* parent);

    Max98927Device(zx_device_t* parent) : DeviceType(parent) { }
    ~Max98927Device() { }

    zx_status_t Bind();
    zx_status_t Initialize();

    // Methods required by the ddk mixins
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* actual);
    void DdkUnbind();
    void DdkRelease();

private:
    // Play a test tone
    void Test();

    // Enable the device
    void Enable();

    // Disable the device
    void Disable();

    // Methods to read/write registers
    uint8_t ReadReg(uint16_t addr);
    void WriteReg(uint16_t addr, uint8_t val);

    // Debug
    void DumpRegs();
};

}  // namespace max98927
}  // namespace audio
