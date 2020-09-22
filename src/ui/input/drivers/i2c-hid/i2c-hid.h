// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_I2C_HID_I2C_HID_H_
#define SRC_UI_INPUT_DRIVERS_I2C_HID_I2C_HID_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/i2c.h>
#include <threads.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

namespace i2c_hid {

// The I2c Hid Command codes.
constexpr uint8_t kResetCommand = 0x01;
constexpr uint8_t kGetReportCommand = 0x02;
constexpr uint8_t kSetReportCommand = 0x03;

// The i2c descriptor that describes the i2c's registers Ids.
// This is populated directly from an I2cRead call, so all
// of the values are in little endian.
struct I2cHidDesc {
  uint16_t wHIDDescLength;
  uint16_t bcdVersion;
  uint16_t wReportDescLength;
  uint16_t wReportDescRegister;
  uint16_t wInputRegister;
  uint16_t wMaxInputLength;
  uint16_t wOutputRegister;
  uint16_t wMaxOutputLength;
  uint16_t wCommandRegister;
  uint16_t wDataRegister;
  uint16_t wVendorID;
  uint16_t wProductID;
  uint16_t wVersionID;
  uint8_t RESERVED[4];
} __PACKED;

class I2cHidbus;
using DeviceType = ddk::Device<I2cHidbus, ddk::Initializable, ddk::Unbindable>;

class I2cHidbus : public DeviceType, public ddk::HidbusProtocol<I2cHidbus, ddk::base_protocol> {
 public:
  explicit I2cHidbus(zx_device_t* device) : DeviceType(device) {}
  ~I2cHidbus() = default;

  // Methods required by the ddk mixins.
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);

  // TODO(fxbug.dev/34503): implement the rest of the HID protocol
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t Bind(ddk::I2cChannel i2c);

  zx_status_t ReadI2cHidDesc(I2cHidDesc* hiddesc);

  // Must be called with i2c_lock held.
  void WaitForReadyLocked() __TA_REQUIRES(i2c_lock_);

  zx_status_t Reset(bool force) __TA_EXCLUDES(i2c_lock_);

 private:
  void Shutdown();

  I2cHidDesc hiddesc_ = {};

  // Signaled when reset received.
  fbl::ConditionVariable i2c_reset_cnd_;

  std::optional<ddk::InitTxn> init_txn_;

  std::atomic_bool worker_thread_started_ = false;
  std::atomic_bool stop_worker_thread_ = false;
  thrd_t worker_thread_;
  zx::interrupt irq_;
  // The functions to be run in the worker thread. They are responsible for initializing the
  // driver and then reading Reports. If the I2c parent driver supports interrupts,
  // then |WorkerThreadIrq| will be used. Otherwise |WorkerThreadNoIrq| will be used and the
  // driver will poll periodically.
  int WorkerThreadIrq();
  int WorkerThreadNoIrq();

  fbl::Mutex ifc_lock_;
  ddk::HidbusIfcProtocolClient ifc_ __TA_GUARDED(ifc_lock_);

  fbl::Mutex i2c_lock_;
  ddk::I2cChannel i2c_ __TA_GUARDED(i2c_lock_);
  // True if reset-in-progress. Initalize as true so no work gets done until this is cleared.
  bool i2c_pending_reset_ __TA_GUARDED(i2c_lock_) = true;
};

}  // namespace i2c_hid

#endif  // SRC_UI_INPUT_DRIVERS_I2C_HID_I2C_HID_H_
