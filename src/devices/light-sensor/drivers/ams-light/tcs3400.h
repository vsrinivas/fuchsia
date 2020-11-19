// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_
#define SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/status.h>

#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/mutex.h>
#include <hid/ambient-light.h>

namespace tcs {

class Tcs3400Device;
using DeviceType = ddk::Device<Tcs3400Device, ddk::Unbindable>;

// Note: the TCS-3400 device is connected via i2c and is not a HID
// device.  This driver reads a collection of data from the data and
// parses it into a message which will be sent up the stack.  This message
// complies with a HID descriptor that was manually scripted (i.e. - not
// reported by the device iteself).
class Tcs3400Device : public DeviceType,
                      public ddk::HidbusProtocol<Tcs3400Device, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  Tcs3400Device(zx_device_t* device, ddk::I2cChannel i2c, gpio_protocol_t gpio, zx::port port)
      : DeviceType(device), i2c_(std::move(i2c)), gpio_(gpio), port_(std::move(port)) {}
  virtual ~Tcs3400Device() = default;

  zx_status_t Bind();
  zx_status_t InitMetadata();
  zx::status<bool> CheckIfSaturated();

  // Methods required by the ddk mixins
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) TA_EXCL(client_input_lock_);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len) TA_EXCL(client_input_lock_, feature_lock_);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len)
      TA_EXCL(feature_lock_);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 protected:
  virtual void ShutDown() TA_EXCL(client_input_lock_);  // protected for unit test.

 private:
  ddk::I2cChannel i2c_ TA_GUARDED(i2c_lock_);
  gpio_protocol_t gpio_ TA_GUARDED(i2c_lock_);
  zx::interrupt irq_;
  thrd_t thread_ = {};
  zx::port port_;
  fbl::Mutex client_input_lock_ TA_ACQ_BEFORE(i2c_lock_);
  fbl::Mutex feature_lock_;
  fbl::Mutex i2c_lock_;
  ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_input_lock_);
  ambient_light_input_rpt_t input_rpt_ TA_GUARDED(client_input_lock_);
  ambient_light_feature_rpt_t feature_rpt_ TA_GUARDED(feature_lock_);
  uint8_t atime_ = 1;
  uint8_t again_ = 1;
  bool saturated_ = false;

  zx_status_t FillInputRpt() TA_REQ(client_input_lock_);
  zx_status_t InitGain(uint8_t gain);
  int Thread();
};
}  // namespace tcs

#endif  // SRC_DEVICES_LIGHT_SENSOR_DRIVERS_AMS_LIGHT_TCS3400_H_
