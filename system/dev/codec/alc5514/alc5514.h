// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/audio-codec.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>

namespace audio {
namespace alc5514 {

class Alc5514Device;
using DeviceType = ddk::Device<Alc5514Device, ddk::Ioctlable, ddk::Unbindable>;

class Alc5514Device : public DeviceType,
                      public ddk::AudioCodecProtocol<Alc5514Device> {
public:
    static fbl::unique_ptr<Alc5514Device> Create(zx_device_t* parent);

    Alc5514Device(zx_device_t* parent) : DeviceType(parent) { }
    ~Alc5514Device() { }

    zx_status_t Bind();
    zx_status_t Initialize();

    // Methods required by the ddk mixins
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* actual);
    void DdkUnbind();
    void DdkRelease();

private:
    void DumpRegs();

    // Methods to read/write registers
    uint32_t ReadReg(uint32_t addr);
    void WriteReg(uint32_t addr, uint32_t val);
    void UpdateReg(uint32_t addr, uint32_t mask, uint32_t bits);
};

}  // namespace alc5514
}  // namespace audio
