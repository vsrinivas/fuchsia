// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ltr-578als.h"

#include <ddktl/pdev.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <zircon/threads.h>

namespace {

// These are the register values used by the existing Cleo code.

constexpr uint8_t kMainCtrlAddress = 0x00;
constexpr uint8_t kPsActiveBit = 0x01;
constexpr uint8_t kAlsActiveBit = 0x02;

constexpr uint8_t kPsLedAddress = 0x01;
constexpr uint8_t kPsLedFreq60Khz = 0x30;
constexpr uint8_t kPsLedCurrent100Ma = 0x06;

constexpr uint8_t kPsPulsesAddress = 0x02;

constexpr uint8_t kPsMeasRateAddress = 0x03;
constexpr uint8_t kPsMeasRate11Bit = 0x18;
constexpr uint8_t kPsMeasRate50Ms = 0x04;

constexpr uint8_t kAlsMeasRateAddress = 0x04;
constexpr uint8_t kAlsMeasRate18Bit = 0x20;
constexpr uint8_t kAlsMeasRate100Ms = 0x02;

constexpr uint8_t kAlsGainAddress = 0x05;
constexpr uint8_t kAlsGain1 = 0x00;

constexpr uint8_t kDefaultRegValues[][2] = {
    {kMainCtrlAddress,    kPsActiveBit | kAlsActiveBit},
    {kPsLedAddress,       kPsLedFreq60Khz | kPsLedCurrent100Ma},
    {kPsPulsesAddress,    16},
    {kPsMeasRateAddress,  kPsMeasRate11Bit | kPsMeasRate50Ms},
    {kAlsMeasRateAddress, kAlsMeasRate18Bit | kAlsMeasRate100Ms},
    {kAlsGainAddress,     kAlsGain1},
};

constexpr uint8_t kPsDataAddress = 0x08;
constexpr uint8_t kAlsDataAddress = 0x0d;

enum PacketKeys {
    kPacketKeyPoll,
    kPacketKeyStop,
    kPacketKeyConfigure,
};

}  // namespace

namespace light {

zx_status_t Ltr578Als::GetInputReport(ltr_578als_input_rpt_t* report) {
    report->rpt_id = LTR_578ALS_RPT_ID_INPUT;

    uint32_t light_data = 0;
    uint16_t proximity_data = 0;

    zx_status_t status;

    {
        fbl::AutoLock lock(&i2c_lock_);
        status = i2c_.ReadSync(kAlsDataAddress, reinterpret_cast<uint8_t*>(&light_data), 3);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to read ambient light registers\n", __FILE__);
            return status;
        }

        status = i2c_.ReadSync(kPsDataAddress, reinterpret_cast<uint8_t*>(&proximity_data), 2);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to read proximity registers\n", __FILE__);
            return status;
        }
    }

    report->ambient_light = le32toh(light_data);
    report->proximity = le16toh(proximity_data);

    return ZX_OK;
}

int Ltr578Als::Thread() {
    zx::time deadline = zx::time::infinite();

    while (1) {
        zx_port_packet_t packet;
        zx_status_t status = port_.wait(deadline, &packet);
        if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
            return thrd_error;
        }

        if (status == ZX_ERR_TIMED_OUT) {
            packet.key = kPacketKeyPoll;
        }

        switch (packet.key) {
        case kPacketKeyStop:
            return thrd_success;

        case kPacketKeyPoll:
            ltr_578als_input_rpt_t report;
            if (GetInputReport(&report) == ZX_OK) {
                fbl::AutoLock lock(&client_lock_);
                if (client_.is_valid()) {
                    client_.IoQueue(&report, sizeof(report));
                }
            }

            __FALLTHROUGH;

        case kPacketKeyConfigure:
            fbl::AutoLock lock(&feature_report_lock_);
            if (feature_report_.interval_ms == 0) {
                deadline = zx::time::infinite();
            } else {
                deadline = zx::deadline_after(zx::msec(feature_report_.interval_ms));
            }
        }
    }

    return thrd_success;
}

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

    zx::port port;
    zx_status_t status = zx::port::create(0, &port);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to create port\n", __FILE__);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Ltr578Als> device(new (&ac) Ltr578Als(parent, *i2c, std::move(port)));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Ltr578Als alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->Init()) != ZX_OK) {
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
    {
        fbl::AutoLock lock(&i2c_lock_);
        for (size_t i = 0; i < countof(kDefaultRegValues); i++) {
            zx_status_t status = i2c_.WriteSync(kDefaultRegValues[i], sizeof(kDefaultRegValues[i]));
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: Failed to configure sensors\n", __FILE__);
                return status;
            }
        }
    }

    return thrd_status_to_zx_status(thrd_create_with_name(
        &thread_,
        [](void* arg) -> int {
            return reinterpret_cast<Ltr578Als*>(arg)->Thread();
        },
        this,
        "ltr578als-thread"));
}

zx_status_t Ltr578Als::HidbusQuery(uint32_t options, hid_info_t* out_info) {
    out_info->dev_num = 0;
    out_info->device_class = HID_DEVICE_CLASS_OTHER;
    out_info->boot_device = false;
    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusStart(const hidbus_ifc_t* ifc) {
    fbl::AutoLock lock(&client_lock_);

    if (client_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    }

    client_ = ddk::HidbusIfcClient(ifc);
    return ZX_OK;
}

void Ltr578Als::HidbusStop() {
    zx_port_packet_t packet = {kPacketKeyStop, ZX_PKT_TYPE_USER, ZX_OK, {}};
    if (port_.queue(&packet) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to queue packet\n", __FILE__);
    }

    thrd_join(thread_, nullptr);

    fbl::AutoLock lock(&client_lock_);
    client_.clear();
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
    if (rpt_type == HID_REPORT_TYPE_INPUT && rpt_id == LTR_578ALS_RPT_ID_INPUT) {
        if (data_size < sizeof(ltr_578als_input_rpt_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        zx_status_t status =
            GetInputReport(reinterpret_cast<ltr_578als_input_rpt_t*>(out_data_buffer));
        if (status != ZX_OK) {
            return status;
        }

        *out_data_actual = sizeof(ltr_578als_input_rpt_t);
    } else if (rpt_type == HID_REPORT_TYPE_FEATURE && rpt_id == LTR_578ALS_RPT_ID_FEATURE) {
        if (data_size < sizeof(ltr_578als_feature_rpt_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        {
            fbl::AutoLock lock(&feature_report_lock_);
            *reinterpret_cast<ltr_578als_feature_rpt_t*>(out_data_buffer) = feature_report_;
        }

        *out_data_actual = sizeof(ltr_578als_feature_rpt_t);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_OK;
}

zx_status_t Ltr578Als::HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                       const void* data_buffer, size_t data_size) {
    if (rpt_type != HID_REPORT_TYPE_FEATURE || rpt_id != LTR_578ALS_RPT_ID_FEATURE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (data_size < sizeof(ltr_578als_feature_rpt_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const ltr_578als_feature_rpt_t* report =
        reinterpret_cast<const ltr_578als_feature_rpt_t*>(data_buffer);

    {
        fbl::AutoLock lock(&feature_report_lock_);
        feature_report_ = *report;
    }

    zx_port_packet packet = {kPacketKeyConfigure, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to queue packet\n", __FILE__);
    }

    return status;
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
