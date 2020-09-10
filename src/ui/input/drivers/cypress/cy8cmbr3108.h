// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_CYPRESS_CY8CMBR3108_H_
#define SRC_UI_INPUT_DRIVERS_CYPRESS_CY8CMBR3108_H_

#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/metadata/touch-buttons.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/hidbus.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/array.h>
#include <fbl/mutex.h>

#include "cy8cmbr3108-reg.h"

namespace cypress {

// I2c register operations
enum { READ = 0x01, WRITE };

// zx_port_packet::key.
constexpr uint64_t kPortKeyShutDown = 0x01;
constexpr uint64_t kPortKeyTouchIrq = 0x02;

class Cy8cmbr3108;
using Cy8cmbr3108Type = ddk::Device<Cy8cmbr3108, ddk::Unbindable>;

class Cy8cmbr3108 : public Cy8cmbr3108Type,
                    public ddk::HidbusProtocol<Cy8cmbr3108, ddk::base_protocol> {
 public:
  explicit Cy8cmbr3108(zx_device_t* parent) : Cy8cmbr3108Type(parent) {}

  virtual ~Cy8cmbr3108() = default;
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Methods required by the ddk mixins.
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) TA_EXCL(client_lock_);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  void HidbusStop() TA_EXCL(client_lock_);
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len) TA_EXCL(client_lock_);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  // Device protocol implementation.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  static bool RunTest(void* ctx, zx_device_t* parent, zx_handle_t channel);
  void ShutDown() TA_EXCL(client_lock_);

 protected:
  // Protected for unit testing.
  virtual zx_status_t InitializeProtocols();
  zx_status_t Init();

  ddk::I2cProtocolClient i2c_;
  ddk::GpioProtocolClient touch_gpio_;
  fbl::Array<touch_button_config_t> buttons_;

 private:
  template <class DerivedType, class IntType, size_t AddrIntSize, class ByteOrder = void>
  zx_status_t RegisterOp(uint32_t op,
                         hwreg::I2cRegisterBase<DerivedType, IntType, AddrIntSize, ByteOrder>& reg);
  bool Test();
  zx_status_t Bind();
  int Thread();

  fbl::Mutex client_lock_;
  ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_lock_);
  thrd_t thread_;
  zx::interrupt touch_irq_;
  zx::port port_;
};

}  // namespace cypress

#endif  // SRC_UI_INPUT_DRIVERS_CYPRESS_CY8CMBR3108_H_
