// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/i2c-channel.h>
#include <ddktl/protocol/hidbus.h>

namespace light {

class Ltr578Als;
using DeviceType = ddk::Device<Ltr578Als>;

class Ltr578Als : public DeviceType, public ddk::HidbusProtocol<Ltr578Als, ddk::base_protocol> {
public:
    static zx_status_t Create(zx_device_t* parent);

    void DdkRelease() { delete this; }

    zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info);
    zx_status_t HidbusStart(const hidbus_ifc_t* ifc);
    void HidbusStop();
    zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void** out_data_buffer,
                                    size_t* data_size);
    zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* out_data_buffer,
                                size_t data_size, size_t* out_data_actual);
    zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void* data_buffer,
                                size_t data_size);
    zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration);
    zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol);
    zx_status_t HidbusSetProtocol(hid_protocol_t protocol);

private:
    Ltr578Als(zx_device_t* parent, ddk::I2cChannel i2c) : DeviceType(parent), i2c_(i2c) {}

    zx_status_t Init();

    ddk::I2cChannel i2c_;
};

}  // namespace light
