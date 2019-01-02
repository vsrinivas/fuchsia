// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ltr-578als.h"

#include <ddktl/pdev.h>
#include <fbl/unique_ptr.h>
#include <hid/ltr-578als.h>

namespace {

constexpr uint8_t kMainCtrlAddress = 0x00;
constexpr uint8_t kPsActiveBit = 0x01;
constexpr uint8_t kAlsActiveBit = 0x02;

constexpr uint8_t kPsDataAddress = 0x08;
constexpr uint8_t kAlsDataAddress = 0x0d;

}  // namespace

namespace light {

zx_status_t Ltr578Als::Create(zx_device_t* parent) {
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get pdev\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::I2cChannel> i2c = pdev.GetI2c(0);
    if (!i2c) {
        zxlogf(ERROR, "%s: Failed to get I2C\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Ltr578Als> device(new (&ac) Ltr578Als(parent, *i2c));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Ltr578Als alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device->Init();
    if (status != ZX_OK) {
        return status;
    }

    if ((status = device->DdkAdd("ltr-578als")) != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

zx_status_t Ltr578Als::Init() {
    constexpr uint8_t kMainCtrlBuf[] = {
        kMainCtrlAddress,
        kPsActiveBit | kAlsActiveBit
    };

    zx_status_t status = i2c_.WriteSync(kMainCtrlBuf, sizeof(kMainCtrlBuf));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to enable sensors\n", __FILE__);
        return status;
    }

    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusQuery(uint32_t options, hid_info_t* out_info) {
    out_info->dev_num = 0;
    out_info->device_class = HID_DEVICE_CLASS_OTHER;
    out_info->boot_device = false;
    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusStart(const hidbus_ifc_t* ifc) {
    // TODO(bradenkell): Implement this.
    return ZX_OK;
}

void Ltr578Als::HidbusStop() {
}

zx_status_t Ltr578Als::HidbusGetDescriptor(hid_description_type_t desc_type, void** out_data_buffer,
                                           size_t* data_size) {
    const uint8_t* desc;
    *data_size = get_ltr_578als_report_desc(&desc);

    fbl::AllocChecker ac;
    uint8_t* buf = new (&ac) uint8_t[*data_size];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(buf, desc, *data_size);
    *out_data_buffer = buf;

    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                       void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
    if (rpt_type != HID_REPORT_TYPE_INPUT || rpt_id != LTR_578ALS_RPT_ID_INPUT) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (data_size < sizeof(ltr_578als_input_rpt_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ltr_578als_input_rpt_t* report = reinterpret_cast<ltr_578als_input_rpt_t*>(out_data_buffer);

    uint8_t light_buf[3];
    zx_status_t status = i2c_.ReadSync(kAlsDataAddress, light_buf, sizeof(light_buf));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to read ambient light registers\n", __FILE__);
        return status;
    }

    uint8_t proximity_buf[2];
    if ((status = i2c_.ReadSync(kPsDataAddress, proximity_buf, sizeof(proximity_buf))) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to read proximity registers\n", __FILE__);
        return status;
    }

    report->rpt_id = rpt_id;
    report->ambient_light = light_buf[0] | (light_buf[1] << 8) | (light_buf[2] << 16);
    report->proximity = static_cast<uint16_t>(proximity_buf[0] | (proximity_buf[1] << 8));

    *out_data_actual = sizeof(ltr_578als_input_rpt_t);

    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                       const void* data_buffer, size_t data_size) {
    // TODO(bradenkell): Implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ltr578Als::HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ltr578Als::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ltr578Als::HidbusGetProtocol(hid_protocol_t* out_protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ltr578Als::HidbusSetProtocol(hid_protocol_t protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace light

extern "C" zx_status_t ltr_578als_bind(void* ctx, zx_device_t* parent) {
    return light::Ltr578Als::Create(parent);
}
