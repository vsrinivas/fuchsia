// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.rtc/cpp/wire.h>
#include <lib/device-protocol/i2c-channel.h>
#include <librtc.h>
#include <librtc_llcpp.h>
#include <stdlib.h>

#include <ddktl/device.h>

#include "src/devices/rtc/drivers/nxp/pcf8563_rtc_bind.h"

namespace FidlRtc = rtc::FidlRtc;

class pcf8563;
using DeviceType = ddk::Device<pcf8563, ddk::Messageable<FidlRtc::Device>::Mixin>;

class pcf8563 : public DeviceType {
 public:
  pcf8563(zx_device_t* parent, ddk::I2cChannel i2c) : DeviceType(parent), i2c_(std::move(i2c)) {}
  void DdkRelease() { delete this; }

  zx::result<FidlRtc::wire::Time> Read() {
    uint8_t write_buf[] = {0x02};
    uint8_t read_buf[7];
    if (zx_status_t status =
            i2c_.WriteReadSync(write_buf, sizeof(write_buf), read_buf, sizeof(read_buf));
        status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(FidlRtc::wire::Time{
        .seconds = from_bcd(read_buf[0] & 0x7f),
        .minutes = from_bcd(read_buf[1] & 0x7f),
        .hours = from_bcd(read_buf[2] & 0x3f),
        .day = from_bcd(read_buf[3] & 0x3f),
        .month = from_bcd(read_buf[5] & 0x1f),
        .year = static_cast<uint16_t>(((read_buf[5] & 0x80) ? 2000 : 1900) + from_bcd(read_buf[6])),
    });
  }

  void Get(GetCompleter::Sync& completer) override {
    if (zx::result result = Read(); result.is_error()) {
      completer.Close(result.status_value());
    } else {
      completer.Reply(result.value());
    }
  }

  zx::result<> Write(FidlRtc::wire::Time rtc) {
    // An invalid time was supplied.
    if (!rtc::IsRtcValid(rtc)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    int year = rtc.year;
    uint8_t century = (year < 2000) ? 0 : 0x80;
    if (century) {
      year -= 2000;
    } else {
      year -= 1900;
    }
    ZX_DEBUG_ASSERT(year < 100);

    uint8_t write_buf[] = {
        0x02,
        to_bcd(rtc.seconds),
        to_bcd(rtc.minutes),
        to_bcd(rtc.hours),
        to_bcd(rtc.day),
        0,  // day of week
        static_cast<uint8_t>(century | to_bcd(rtc.month)),
        to_bcd(static_cast<uint8_t>(year)),
    };

    return zx::make_result(i2c_.WriteReadSync(write_buf, sizeof(write_buf), nullptr, 0));
  }

  void Set(SetRequestView request, SetCompleter::Sync& completer) override {
    completer.Reply(Write(request->rtc).status_value());
  }

 private:
  ddk::I2cChannel i2c_;
};

static zx_status_t pcf8563_bind(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "%s: failed to acquire i2c", __FUNCTION__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::unique_ptr rtc = std::make_unique<pcf8563>(parent, std::move(i2c));
  if (rtc == nullptr) {
    zxlogf(ERROR, "%s: failed to create device", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  zx::result time = rtc->Read();
  if (time.is_error()) {
    zxlogf(ERROR, "%s: failed to read clock", __FUNCTION__);
    return time.status_value();
  }
  if (zx::result result = rtc->Write(rtc::SanitizeRtc(parent, time.value())); result.is_error()) {
    zxlogf(ERROR, "%s: failed to write clock", __FUNCTION__);
    return result.status_value();
  }

  if (zx_status_t status = rtc->DdkAdd(ddk::DeviceAddArgs("rtc").set_proto_id(ZX_PROTOCOL_RTC));
      status != ZX_OK) {
    return status;
  }

  // We've passed ownership to the framework.
  __UNUSED pcf8563* ptr = rtc.release();

  return ZX_OK;
}

static zx_driver_ops_t pcf8563_rtc_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = pcf8563_bind,
};

ZIRCON_DRIVER(pcf8563_rtc, pcf8563_rtc_ops, "pcf8563_rtc", "0.1");
