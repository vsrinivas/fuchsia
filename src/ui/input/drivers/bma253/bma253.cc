// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bma253.h"

#include <endian.h>
#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

namespace {

constexpr uint8_t kPmuRangeAddress = 0x0f;
constexpr uint8_t kPmuRange4G = 0b0101;

constexpr uint8_t kPmuBwAddress = 0x10;
constexpr uint8_t kPmuBw62_5Hz = 0b01011;

constexpr uint8_t kDefaultRegValues[][2] = {
    {kPmuRangeAddress, kPmuRange4G},
    {kPmuBwAddress, kPmuBw62_5Hz},
};

constexpr uint8_t kAccdAddress = 0x02;
constexpr int kAccdShift = 4;

constexpr uint8_t kAccdTempAddress = 0x08;

}  // namespace

namespace accel {

zx_status_t Bma253::GetInputReport(bma253_input_rpt_t* report) {
  report->rpt_id = BMA253_RPT_ID_INPUT;

  uint16_t accel_data[3];
  uint8_t temp_data;

  zx_status_t status;

  {
    fbl::AutoLock lock(&i2c_lock_);
    status =
        i2c_.ReadSync(kAccdAddress, reinterpret_cast<uint8_t*>(accel_data), sizeof(accel_data));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read acceleration registers\n", __FILE__);
      return status;
    }

    status =
        i2c_.ReadSync(kAccdTempAddress, reinterpret_cast<uint8_t*>(&temp_data), sizeof(temp_data));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read temperature register\n", __FILE__);
      return status;
    }
  }

  report->acceleration_x = static_cast<uint16_t>(le16toh(accel_data[0]) >> kAccdShift);
  report->acceleration_y = static_cast<uint16_t>(le16toh(accel_data[1]) >> kAccdShift);
  report->acceleration_z = static_cast<uint16_t>(le16toh(accel_data[2]) >> kAccdShift);
  report->temperature = temp_data;

  return ZX_OK;
}

zx_status_t Bma253::Create(void* ctx, zx_device_t* parent) {
  i2c_protocol_t i2c;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_I2C\n", __FILE__);
    return status;
  }

  zx::port port;
  status = zx::port::create(0, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create port\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Bma253> device(new (&ac) Bma253(parent, &i2c, std::move(port)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Bma253 alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("bma253")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t Bma253::Init() {
  fbl::AutoLock lock(&i2c_lock_);
  for (size_t i = 0; i < countof(kDefaultRegValues); i++) {
    zx_status_t status = i2c_.WriteSync(kDefaultRegValues[i], sizeof(kDefaultRegValues[i]));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to configure sensor\n", __FILE__);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Bma253::HidbusQuery(uint32_t options, hid_info_t* out_info) {
  out_info->dev_num = 0;
  out_info->device_class = HID_DEVICE_CLASS_OTHER;
  out_info->boot_device = false;
  return ZX_OK;
}

zx_status_t Bma253::HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                        size_t data_size, size_t* out_data_actual) {
  const uint8_t* desc;
  size_t desc_size = get_bma253_report_desc(&desc);

  if (data_size < desc_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, desc, desc_size);
  *out_data_actual = desc_size;

  return ZX_OK;
}

zx_status_t Bma253::HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                    void* out_data_buffer, size_t data_size,
                                    size_t* out_data_actual) {
  if (rpt_type == HID_REPORT_TYPE_INPUT && rpt_id == BMA253_RPT_ID_INPUT) {
    if (data_size < sizeof(bma253_input_rpt_t)) {
      return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = GetInputReport(reinterpret_cast<bma253_input_rpt_t*>(out_data_buffer));
    if (status != ZX_OK) {
      return status;
    }

    *out_data_actual = sizeof(bma253_input_rpt_t);
  } else if (rpt_type == HID_REPORT_TYPE_FEATURE && rpt_id == BMA253_RPT_ID_FEATURE) {
    if (data_size < sizeof(bma253_feature_rpt_t)) {
      return ZX_ERR_INVALID_ARGS;
    }

    bma253_feature_rpt_t* report = reinterpret_cast<bma253_feature_rpt_t*>(out_data_buffer);
    report->rpt_id = BMA253_RPT_ID_FEATURE;
    report->interval_ms = simple_hid_.GetReportInterval();

    *out_data_actual = sizeof(bma253_feature_rpt_t);
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t Bma253::HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                    const void* data_buffer, size_t data_size) {
  if (rpt_type != HID_REPORT_TYPE_FEATURE || rpt_id != BMA253_RPT_ID_FEATURE) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (data_size < sizeof(bma253_feature_rpt_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  const bma253_feature_rpt_t* report = reinterpret_cast<const bma253_feature_rpt_t*>(data_buffer);
  return simple_hid_.SetReportInterval(report->interval_ms);
}

zx_status_t Bma253::HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Bma253::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Bma253::HidbusGetProtocol(hid_protocol_t* out_protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Bma253::HidbusSetProtocol(hid_protocol_t protocol) { return ZX_ERR_NOT_SUPPORTED; }

}  // namespace accel

static constexpr zx_driver_ops_t bma253_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = accel::Bma253::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(bma253, bma253_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BOSCH_BMA253), ZIRCON_DRIVER_END(bma253)
