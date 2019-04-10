// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fuchsia/hardware/backlight/c/fidl.h>

namespace ti {

#define LOG_ERROR(fmt, ...)    zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)     zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_SPEW(fmt, ...)     zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_TRACE              zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

class Lp8556Device;
using DeviceType = ddk::Device<Lp8556Device, ddk::Unbindable, ddk::Messageable>;

class Lp8556Device : public DeviceType,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_BACKLIGHT> {

public:

    Lp8556Device(zx_device_t* parent) : DeviceType(parent) {}

    zx_status_t Bind();

    // Methods requried by the ddk mixins
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    zx_status_t GetBacklightState(bool* power, uint8_t* brightness);
    zx_status_t SetBacklightState(bool power, uint8_t brightness);

private:
    i2c_protocol_t i2c_ = {};
    // brightness is set to maximum from bootloader.
    uint8_t brightness_ = 0xFF;
    bool power_ = true;

};

} // namespace ti