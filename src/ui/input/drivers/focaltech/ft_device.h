// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_DEVICE_H_
#define SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_DEVICE_H_

#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>

#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <ddktl/protocol/test.h>
#include <fbl/mutex.h>
#include <hid/ft3x27.h>
#include <hid/ft5726.h>
#include <hid/ft6336.h>

enum {
  FT_INT_PIN,
  FT_RESET_PIN,
  FT_PIN_COUNT,
};

// clang-format off
#define FTS_REG_CURPOINT                    0x02
#define FTS_REG_FINGER_START                0x03
#define FTS_REG_INT_CNT                     0x8F
#define FTS_REG_FLOW_WORK_CNT               0x91
#define FTS_REG_WORKMODE                    0x00
#define FTS_REG_WORKMODE_FACTORY_VALUE      0x40
#define FTS_REG_WORKMODE_WORK_VALUE         0x00
#define FTS_REG_ESDCHECK_DISABLE            0x8D
#define FTS_REG_CHIP_ID                     0xA3
#define FTS_REG_CHIP_ID2                    0x9F
#define FTS_REG_POWER_MODE                  0xA5
#define FTS_REG_POWER_MODE_SLEEP_VALUE      0x03
#define FTS_REG_FW_VER                      0xA6
#define FTS_REG_VENDOR_ID                   0xA8
#define FTS_REG_LCD_BUSY_NUM                0xAB
#define FTS_REG_FACE_DEC_MODE_EN            0xB0
#define FTS_REG_FACE_DEC_MODE_STATUS        0x01
#define FTS_REG_IDE_PARA_VER_ID             0xB5
#define FTS_REG_IDE_PARA_STATUS             0xB6
#define FTS_REG_GLOVE_MODE_EN               0xC0
#define FTS_REG_COVER_MODE_EN               0xC1
#define FTS_REG_CHARGER_MODE_EN             0x8B
#define FTS_REG_GESTURE_EN                  0xD0
#define FTS_REG_GESTURE_OUTPUT_ADDRESS      0xD3
#define FTS_REG_MODULE_ID                   0xE3
#define FTS_REG_LIC_VER                     0xE4
#define FTS_REG_ESD_SATURATE                0xED
// clang-format on

namespace ft {
class FtDevice : public ddk::Device<FtDevice, ddk::Unbindable>,
                 public ddk::HidbusProtocol<FtDevice, ddk::base_protocol> {
 public:
  FtDevice(zx_device_t* device);

  static zx_status_t Create(void* ctx, zx_device_t* device);

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

 private:
  /* Note: the focaltouch device is connected via i2c and is NOT a HID
      device.  This driver reads a collection of data from the data and
      parses it into a message which will be sent up the stack.  This message
      complies with a HID descriptor that manually scripted (i.e. - not
      reported by the device iteself).
  */
  // Number of touch points this device can report simultaneously
  static constexpr uint32_t kMaxPoints = 5;
  // Size of each individual touch record (note: there are kMaxPoints of
  //  them) on the i2c bus.  This is not the HID report size.
  static constexpr uint32_t kFingerRptSize = 6;

  static constexpr size_t kMaxI2cTransferLength = 8;

  zx_status_t Init();
  zx_status_t ShutDown() __TA_EXCLUDES(client_lock_);

  uint8_t Read(uint8_t addr);
  zx_status_t Read(uint8_t addr, uint8_t* buf, size_t len);

  int Thread();

  ft3x27_touch_t ft_rpt_ __TA_GUARDED(client_lock_);
  void ParseReport(ft3x27_finger_t* rpt, uint8_t* buf);

  gpio_protocol_t gpios_[FT_PIN_COUNT];
  zx::interrupt irq_;
  i2c_protocol_t i2c_;

  thrd_t thread_;
  std::atomic<bool> running_;

  fbl::Mutex client_lock_;
  ddk::HidbusIfcProtocolClient client_ __TA_GUARDED(client_lock_);

  const uint8_t* descriptor_ = nullptr;
  size_t descriptor_len_ = 0;
};
}  // namespace ft

#endif  // SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_DEVICE_H_
