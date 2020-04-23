// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ltr-578als.h"

#include <endian.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

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
    {kMainCtrlAddress, kPsActiveBit | kAlsActiveBit},
    {kPsLedAddress, kPsLedFreq60Khz | kPsLedCurrent100Ma},
    {kPsPulsesAddress, 16},
    {kPsMeasRateAddress, kPsMeasRate11Bit | kPsMeasRate50Ms},
    {kAlsMeasRateAddress, kAlsMeasRate18Bit | kAlsMeasRate100Ms},
    {kAlsGainAddress, kAlsGain1},
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
      zxlogf(ERROR, "%s: Failed to read ambient light registers", __FILE__);
      return status;
    }

    status = i2c_.ReadSync(kPsDataAddress, reinterpret_cast<uint8_t*>(&proximity_data), 2);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read proximity registers", __FILE__);
      return status;
    }
  }

  report->ambient_light = le32toh(light_data);
  report->proximity = le16toh(proximity_data);

  return ZX_OK;
}

zx_status_t Ltr578Als::Create(void* ctx, zx_device_t* parent) {
  i2c_protocol_t i2c;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_I2C", __FILE__);
    return status;
  }

  zx::port port;
  status = zx::port::create(0, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create port", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Ltr578Als> device(new (&ac) Ltr578Als(parent, &i2c, std::move(port)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Ltr578Als alloc failed", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("ltr-578als")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __FILE__);
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
        zxlogf(ERROR, "%s: Failed to configure sensors", __FILE__);
        return status;
      }
    }
  }

  return ZX_OK;
}

zx_status_t Ltr578Als::HidbusQuery(uint32_t options, hid_info_t* out_info) {
  out_info->dev_num = 0;
  out_info->device_class = HID_DEVICE_CLASS_OTHER;
  out_info->boot_device = false;
  return ZX_OK;
}

zx_status_t Ltr578Als::HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                           size_t data_size, size_t* out_data_actual) {
  const uint8_t* desc;
  size_t desc_size = get_ltr_578als_report_desc(&desc);

  if (data_size < desc_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, desc, desc_size);
  *out_data_actual = desc_size;
  return ZX_OK;
}

zx_status_t Ltr578Als::HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                       void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
  if (rpt_type == HID_REPORT_TYPE_INPUT && rpt_id == LTR_578ALS_RPT_ID_INPUT) {
    if (data_size < sizeof(ltr_578als_input_rpt_t)) {
      return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = GetInputReport(reinterpret_cast<ltr_578als_input_rpt_t*>(out_data_buffer));
    if (status != ZX_OK) {
      return status;
    }

    *out_data_actual = sizeof(ltr_578als_input_rpt_t);
  } else if (rpt_type == HID_REPORT_TYPE_FEATURE && rpt_id == LTR_578ALS_RPT_ID_FEATURE) {
    if (data_size < sizeof(ltr_578als_feature_rpt_t)) {
      return ZX_ERR_INVALID_ARGS;
    }

    ltr_578als_feature_rpt_t* report = reinterpret_cast<ltr_578als_feature_rpt_t*>(out_data_buffer);
    report->rpt_id = LTR_578ALS_RPT_ID_FEATURE;
    report->interval_ms = simple_hid_.GetReportInterval();

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
  return simple_hid_.SetReportInterval(report->interval_ms);
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

zx_status_t Ltr578Als::HidbusSetProtocol(hid_protocol_t protocol) { return ZX_ERR_NOT_SUPPORTED; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Ltr578Als::Create;
  return ops;
}();

}  // namespace light

// clang-format off
ZIRCON_DRIVER_BEGIN(ltr_578als, light::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_LITE_ON_ALS),
ZIRCON_DRIVER_END(ltr_578als)
    // clang-format on
