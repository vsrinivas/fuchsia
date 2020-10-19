// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RTC_DRIVERS_PL031_RTC_PL031_RTC_H_
#define SRC_DEVICES_RTC_DRIVERS_PL031_RTC_PL031_RTC_H_

#include <fuchsia/hardware/rtc/c/fidl.h>
#include <lib/fidl-utils/bind.h>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/device.h>

namespace rtc {

class Pl031;
using RtcDeviceType = ddk::Device<Pl031, ddk::Messageable>;

struct Pl031Regs {
  uint32_t dr;
  uint32_t mr;
  uint32_t lr;
  uint32_t cr;
  uint32_t msc;
  uint32_t ris;
  uint32_t mis;
  uint32_t icr;
};

class Pl031 : public RtcDeviceType {
 public:
  static zx_status_t Bind(void*, zx_device_t* dev);

  explicit Pl031(zx_device_t* parent);
  ~Pl031() = default;

  // DDK bindings.
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

 private:
  // fuchsia.hardware.rtc FIDL interface.
  zx_status_t FidlGetRtc(fidl_txn_t* txn);
  zx_status_t FidlSetRtc(const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn);

  static const fuchsia_hardware_rtc_Device_ops* Ops() {
    using Binder = fidl::Binder<Pl031>;
    static const fuchsia_hardware_rtc_Device_ops kOps = {
        .Get = Binder::BindMember<&Pl031::FidlGetRtc>,
        .Set = Binder::BindMember<&Pl031::FidlSetRtc>,
    };
    return &kOps;
  }

  mmio_buffer_t mmio_;
  MMIO_PTR Pl031Regs* regs_;
};

}  // namespace rtc

#endif  // SRC_DEVICES_RTC_DRIVERS_PL031_RTC_PL031_RTC_H_
