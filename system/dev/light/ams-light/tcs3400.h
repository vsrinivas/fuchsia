// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>

#include <fbl/mutex.h>

#include <hid/tcs3400.h>

#define TCS3400_POLL_SLEEP_SECS 2

namespace tcs {

class Tcs3400Device;
using DeviceType = ddk::Device<Tcs3400Device, ddk::Unbindable, ddk::Readable>;

// Note: the TCS-3400 device is connected via i2c and is not a HID
// device.  This driver reads a collection of data from the data and
// parses it into a message which will be sent up the stack.  This message
// complies with a HID descriptor that was manually scripted (i.e. - not
// reported by the device iteself).
class Tcs3400Device : public DeviceType,
                      public ddk::HidBusProtocol<Tcs3400Device> {
public:
    Tcs3400Device(zx_device_t* device)
        : DeviceType(device) {}

    zx_status_t Bind();
    int Thread();

    // Methods required by the ddk mixins
    zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);

    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy) __TA_EXCLUDES(proxy_lock_);
    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info);
    void HidBusStop();
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len, size_t* out_len);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);

    void DdkUnbind();
    void DdkRelease();

private:
    // Only one I2c channel is passed to this driver, so index should always be zero.
    static constexpr uint32_t kI2cIndex = 0;
    i2c_protocol_t i2c_;
    thrd_t thread_;
    fbl::atomic<bool> running_;
    fbl::Mutex proxy_lock_;
    ddk::HidBusIfcProxy proxy_ __TA_GUARDED(proxy_lock_);
    tcs3400_data_t tcs_rpt_ __TA_GUARDED(proxy_lock_);

    zx_status_t FillRpt() __TA_REQUIRES(proxy_lock_);
    void ShutDown() __TA_EXCLUDES(proxy_lock_);
};
}
