// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIGHT_SENSOR_DRIVERS_LITE_ON_LTR_578ALS_H_
#define SRC_DEVICES_LIGHT_SENSOR_DRIVERS_LITE_ON_LTR_578ALS_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simplehid/simplehid.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/port.h>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/mutex.h>
#include <hid/ltr-578als.h>

namespace light {

class Ltr578Als;
using DeviceType = ddk::Device<Ltr578Als>;

class Ltr578Als : public DeviceType, public ddk::HidbusProtocol<Ltr578Als, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t HidbusQuery(uint32_t options, hid_info_t* out_info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) { return simple_hid_.HidbusStart(ifc); }
  void HidbusStop() { simple_hid_.HidbusStop(); }
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* out_data_buffer,
                              size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void* data_buffer,
                              size_t data_size);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol);
  zx_status_t HidbusSetProtocol(hid_protocol_t protocol);

  // Visible for testing.
  Ltr578Als(zx_device_t* parent, ddk::I2cChannel i2c, zx::port port)
      : DeviceType(parent), i2c_(i2c) {
    simple_hid_ = simplehid::SimpleHid<ltr_578als_input_rpt_t>(
        std::move(port), [this](ltr_578als_input_rpt_t* report) { return GetInputReport(report); });
  }

  // Visible for testing.
  zx_status_t Init();

 private:
  zx_status_t GetInputReport(ltr_578als_input_rpt_t* report);

  fbl::Mutex i2c_lock_;
  ddk::I2cChannel i2c_ TA_GUARDED(i2c_lock_);

  simplehid::SimpleHid<ltr_578als_input_rpt_t> simple_hid_;
};

}  // namespace light

#endif  // SRC_DEVICES_LIGHT_SENSOR_DRIVERS_LITE_ON_LTR_578ALS_H_
