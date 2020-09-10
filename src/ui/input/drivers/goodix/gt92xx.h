// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_
#define SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <atomic>
#include <utility>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <hid/gt92xx.h>

// clang-format off
#define GT_REG_SLEEP            0x8040
#define GT_REG_CONFIG_DATA      0x8047
#define GT_REG_MAX_X_LO         0x8048
#define GT_REG_MAX_X_HI         0x8049
#define GT_REG_MAX_Y_LO         0x804a
#define GT_REG_MAX_Y_HI         0x804b
#define GT_REG_NUM_FINGERS      0x804c

#define GT_REG_CONFIG_REFRESH   0x812a
#define GT_REG_VERSION          0x8140
#define GT_REG_SENSOR_ID        0x814a
#define GT_REG_TOUCH_STATUS     0x814e
#define GT_REG_REPORTS          0x814f

#define GT_REG_FIRMWARE         0x41e4
#define GT_FIRMWARE_MAGIC       0xbe

#define GT_REG_TOUCH_STATUS_READY   0x80
// clang-format on

namespace goodix {

class Gt92xxDevice : public ddk::Device<Gt92xxDevice, ddk::Unbindable>,
                     public ddk::HidbusProtocol<Gt92xxDevice, ddk::base_protocol> {
 public:
  Gt92xxDevice(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient intr,
               ddk::GpioProtocolClient reset)
      : ddk::Device<Gt92xxDevice, ddk::Unbindable>(device),
        i2c_(std::move(i2c)),
        int_gpio_(std::move(intr)),
        reset_gpio_(std::move(reset)) {}

  static fbl::Vector<uint8_t> GetConfData();

  static zx_status_t Create(zx_device_t* device);

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn) __TA_EXCLUDES(client_lock_);

  // Hidbus required methods
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) __TA_EXCLUDES(client_lock_);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info) __TA_EXCLUDES(client_lock_);

 protected:
  zx_status_t Init();
  std::atomic<bool> running_;
  zx::interrupt irq_;
  int Thread();

 private:
  // Format of data as it is read from the device
  struct FingerReport {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t reserved;
  } __PACKED;

  static constexpr uint32_t kMaxPoints = 5;

  zx_status_t ShutDown() __TA_EXCLUDES(client_lock_);
  // performs hardware reset using gpio
  void HWReset();

  uint8_t Read(uint16_t addr);
  zx_status_t Read(uint16_t addr, uint8_t* buf, uint8_t len);
  zx_status_t Write(uint16_t addr, uint8_t val);

  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient int_gpio_;
  ddk::GpioProtocolClient reset_gpio_;

  gt92xx_touch_t gt_rpt_ __TA_GUARDED(client_lock_);
  thrd_t thread_;
  fbl::Mutex client_lock_;
  ddk::HidbusIfcProtocolClient client_ __TA_GUARDED(client_lock_);
};
}  // namespace goodix

#endif  // SRC_UI_INPUT_DRIVERS_GOODIX_GT92XX_H_
