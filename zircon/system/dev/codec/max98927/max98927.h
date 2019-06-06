// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <zircon/types.h>
#include <fbl/unique_ptr.h>

namespace audio {
namespace max98927 {

class Max98927Device;
using DeviceType = ddk::Device<Max98927Device, ddk::Messageable, ddk::Unbindable>;

class Max98927Device : public DeviceType,
                       public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
public:
    static fbl::unique_ptr<Max98927Device> Create(zx_device_t* parent);

    Max98927Device(zx_device_t* parent) : DeviceType(parent) { }
    ~Max98927Device() { }

    zx_status_t Bind();
    zx_status_t Initialize();

    // Methods required by the ddk mixins
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    void DdkUnbind();
    void DdkRelease();

private:
    zx_status_t FidlSetEnabled(bool enable);

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

    i2c_protocol_t i2c_;
};

}  // namespace max98927
}  // namespace audio
