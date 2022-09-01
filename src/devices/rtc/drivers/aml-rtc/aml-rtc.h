// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RTC_DRIVERS_AML_RTC_AML_RTC_H_
#define SRC_DEVICES_RTC_DRIVERS_AML_RTC_AML_RTC_H_

#include <fidl/fuchsia.hardware.rtc/cpp/wire.h>
#include <lib/ddk/device.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>

namespace rtc {

namespace FidlRtc = fuchsia_hardware_rtc;
class AmlRtc;
using RtcDeviceType = ddk::Device<AmlRtc, ddk::Messageable<FidlRtc::Device>::Mixin>;

struct AmlRtcRegs {
  uint32_t ctrl;        /* Control RTC -RW */
  uint32_t counter;     /* Program RTC counter initial value -RW */
  uint32_t alarm0;      /* Program RTC alarm0 value -RW */
  uint32_t alarm1;      /* Program RTC alarm1 value -RW */
  uint32_t alarm2;      /* Program RTC alarm2 value -RW */
  uint32_t alarm3;      /* Program RTC alarm3 value -RW */
  uint32_t sec_adjust;  /* Control second-based timing adjustment -RW */
  uint32_t widen_val;   /* Cross clock domain widen val -RW */
  uint32_t int_mask;    /* RTC interrupt mask -RW  */
  uint32_t int_clr;     /* Clear RTC interrupt -RW */
  uint32_t oscin_ctrl0; /* Control RTC clk from 24M -RW */
  uint32_t oscin_ctrl1; /* Control RTC clk from 24M -RW */
  uint32_t int_status;  /* RTC interrupt status -R */
  uint32_t real_time;   /* RTC counter value -R */
};

/* RTC_CTRL Bit[8]: 0 - select 32K osilator, 1 - select 24M osilator */
#define RTC_OSC_SEL_BIT (8)
/* RTC_CTRL Bit[12]: 0 - disable rtc, 1 - enable rtc */
#define RTC_ENABLE_BIT (12)
/* 0: freq_out= freq_in/ N0;1: freq_out= freq_in/((N0*M0+N1*M1)/(M0+M1)) */
#define FREQ_OUT_SELECT (28)
/* clock in gate en */
#define CLK_IN_GATE_EN (31)
/* clock div M0 */
#define CLK_DIV_M0 (0)
/* clock div M1 */
#define CLK_DIV_M1 (12)

#define RTC_CTRL (0)                /* Control RTC -RW */
#define RTC_COUNTER_REG (1 << 2)    /* Program RTC counter initial value -RW */
#define RTC_ALARM0_REG (2 << 2)     /* Program RTC alarm0 value -RW */
#define RTC_ALARM1_REG (3 << 2)     /* Program RTC alarm1 value -RW */
#define RTC_ALARM2_REG (4 << 2)     /* Program RTC alarm2 value -RW */
#define RTC_ALARM3_REG (5 << 2)     /* Program RTC alarm3 value -RW */
#define RTC_SEC_ADJUST_REG (6 << 2) /* Control second-based timing adjustment -RW */
#define RTC_WIDEN_VAL (7 << 2)      /* Cross clock domain widen val -RW */
#define RTC_INT_MASK (8 << 2)       /* RTC interrupt mask -RW */
#define RTC_INT_CLR (9 << 2)        /* Clear RTC interrupt -RW */
#define RTC_OSCIN_CTRL0 (10 << 2)   /* Control RTC clk from 24M -RW */
#define RTC_OSCIN_CTRL1 (11 << 2)   /* Control RTC clk from 24M -RW */
#define RTC_INT_STATUS (12 << 2)    /* RTC interrupt status -R */
#define RTC_REAL_TIME (13 << 2)     /* RTC counter value -R */

class AmlRtc : public RtcDeviceType {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* device);

  AmlRtc(zx_device_t* parent, fdf::MmioBuffer mmio);
  ~AmlRtc() = default;

  // fidl::WireServer<FidlRtc::Device>:
  void Get(GetCompleter::Sync& completer) override;
  void Set(SetRequestView request, SetCompleter::Sync& completer) override;

  // DDK bindings.
  void DdkRelease();

 private:
  zx_status_t SetRtc(FidlRtc::wire::Time rtc);

  fdf::MmioBuffer mmio_;
  MMIO_PTR AmlRtcRegs* regs_;
};

}  // namespace rtc

#endif  // SRC_DEVICES_RTC_DRIVERS_AML_RTC_AML_RTC_H_
