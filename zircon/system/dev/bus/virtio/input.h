// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_H_
#define ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_H_

#include <fuchsia/hardware/pty/c/fidl.h>
#include <stdlib.h>

#include <memory>

#include <ddk/io-buffer.h>
#include <ddk/protocol/hidbus.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <hid/boot.h>
#include <virtio/input.h>

#include "device.h"
#include "input_kbd.h"
#include "input_touch.h"
#include "ring.h"

namespace virtio {

class InputDevice : public Device,
                    public ddk::Device<InputDevice, ddk::Messageable>,
                    public ddk::HidbusProtocol<InputDevice, ddk::base_protocol> {
 public:
  InputDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend);
  virtual ~InputDevice();

  zx_status_t Init() override;

  void IrqRingUpdate() override;
  void IrqConfigChange() override;
  const char* tag() const override { return "virtio-input"; }

  // DDK driver hooks
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  // Unsupported calls:
  zx_status_t HidbusGetReport(hid_report_type_t rpt_type, uint8_t rpt_id, void* out_data_buffer,
                              size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusSetReport(hid_report_type_t rpt_type, uint8_t rpt_id, const void* data_buffer,
                              size_t data_size);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* out_duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(hid_protocol_t* out_protocol);
  zx_status_t HidbusSetProtocol(hid_protocol_t protocol);

 private:
  void ReceiveEvent(virtio_input_event_t* event);

  void SelectConfig(uint8_t select, uint8_t subsel);

  virtio_input_config_t config_;

  static const size_t kEventCount = 64;
  io_buffer_t buffers_[kEventCount];

  fbl::Mutex lock_;

  uint8_t dev_class_;
  hidbus_ifc_protocol_t hidbus_ifc_;

  std::unique_ptr<HidDevice> hid_device_;
  Ring vring_ = {this};
};

}  // namespace virtio

#endif  // ZIRCON_SYSTEM_DEV_BUS_VIRTIO_INPUT_H_
