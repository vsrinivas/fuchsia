// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HIDCTL_HIDCTL_H_
#define SRC_UI_INPUT_DRIVERS_HIDCTL_HIDCTL_H_

#include <fidl/fuchsia.hardware.hidctl/cpp/wire.h>
#include <fuchsia/hardware/hidbus/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/zx/socket.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>

namespace hidctl {

class HidCtl;
using DeviceType = ddk::Device<HidCtl, ddk::Messageable<fuchsia_hardware_hidctl::Device>::Mixin>;
class HidCtl : public DeviceType {
 public:
  HidCtl(zx_device_t* device);
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease();

  void MakeHidDevice(MakeHidDeviceRequestView request, MakeHidDeviceCompleter::Sync& completer);
};

class HidDevice : public ddk::Device<HidDevice, ddk::Initializable, ddk::Unbindable>,
                  public ddk::HidbusProtocol<HidDevice, ddk::base_protocol> {
 public:
  HidDevice(zx_device_t* device, const fuchsia_hardware_hidctl::wire::HidCtlConfig& config,
            fbl::Array<const uint8_t> report_desc, zx::socket data);
  ~HidDevice();

  void DdkRelease();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);

  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, uint8_t* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  int Thread();

 private:
  zx_status_t Recv(uint8_t* buffer, uint32_t capacity);

  bool boot_device_;
  uint8_t dev_class_;
  fbl::Array<const uint8_t> report_desc_;
  uint32_t mtu_ = 256;  // TODO: set this based on report_desc_

  fbl::Mutex lock_;
  ddk::HidbusIfcProtocolClient client_ __TA_GUARDED(lock_);
  std::optional<ddk::UnbindTxn> unbind_txn_ __TA_GUARDED(lock_);
  zx::socket data_;
  thrd_t thread_;
};

}  // namespace hidctl

#endif  // SRC_UI_INPUT_DRIVERS_HIDCTL_HIDCTL_H_
