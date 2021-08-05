// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/rtc/drivers/intel-rtc/intel-rtc.h"

#include <fuchsia/hardware/rtc/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/inout.h>
#include <librtc.h>
#include <librtc_llcpp.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"
#include "src/devices/rtc/drivers/intel-rtc/intel_rtc_bind.h"

namespace intel_rtc {
constexpr uint32_t kPortCount = 2;

#ifdef FOR_TEST
void TestOutp(uint16_t port, uint8_t value);
uint8_t TestInp(uint16_t port);
#define outp TestOutp
#define inp TestInp
#endif

uint8_t RtcDevice::ReadRegRaw(Registers reg) {
  outp(port_base_ + kIndexOffset, reg);
  return inp(port_base_ + kDataOffset);
}

void RtcDevice::WriteRegRaw(Registers reg, uint8_t val) {
  outp(port_base_ + kIndexOffset, reg);
  outp(port_base_ + kDataOffset, val);
}

uint8_t RtcDevice::ReadReg(Registers reg) {
  uint8_t val = ReadRegRaw(reg);
  return is_bcd_ ? from_bcd(val) : val;
}

void RtcDevice::WriteReg(Registers reg, uint8_t val) {
  WriteRegRaw(reg, is_bcd_ ? to_bcd(val) : val);
}

uint8_t RtcDevice::ReadHour() {
  uint8_t data = ReadRegRaw(kRegHours);

  // The high bit is set for PM and unset for AM in when not in 24-hour mode.
  bool pm = data & kHourPmBit;
  data &= ~kHourPmBit;

  uint8_t hour = is_bcd_ ? from_bcd(data) : data;

  if (is_24_hour_) {
    return hour;
  }

  if (pm) {
    hour += 12;
  }

  switch (hour) {
    case 24:  // Fix up 12 pm.
      return 12;
    case 12:  // Fix up 12 am.
      return 0;
    default:
      return hour;
  }
}

void RtcDevice::WriteHour(uint8_t hour) {
  bool pm = hour > 11;
  uint8_t data = 0;
  if (!is_24_hour_) {
    if (pm) {
      data |= kHourPmBit;
      hour -= 12;
    }
    if (hour == 0) {
      hour = 12;
    }
  }

  data |= is_bcd_ ? to_bcd(hour) : hour;

  WriteRegRaw(kRegHours, data);
}

// Retrieve the hour format and data mode bits. Note that on some
// platforms (including the acer) these bits cannot be reliably
// written. So we must instead parse and provide the data in whatever
// format is given to us.
void RtcDevice::CheckRtcMode() {
  uint8_t reg_b = ReadRegRaw(kRegB);
  // if HOUR_FORMAT_BIT is set, then the RTC is in 24-hour mode.
  is_24_hour_ = (reg_b & kRegBHourFormatBit) == kRegBHourFormatBit;
  // if DATA_MODE_BIT is set, then the RTC uses binary values.
  is_bcd_ = !(reg_b & kRegBDataFormatBit);
}

FidlRtc::wire::Time RtcDevice::ReadTime() {
  std::scoped_lock lock(time_lock_);
  FidlRtc::wire::Time result;
  CheckRtcMode();

  while (ReadRegRaw(kRegA) & kRegAUpdateInProgressBit) {
    // The datasheet says "the entire cycle does not take more than 1984 uS to complete".
    // This should be plenty of time for the RTC to update itself.
    zx::nanosleep(zx::deadline_after(zx::usec(2000)));
  }

  result.seconds = ReadReg(kRegSeconds);
  result.minutes = ReadReg(kRegMinutes);
  result.hours = ReadHour();

  result.day = ReadReg(kRegDayOfMonth);
  result.month = ReadReg(kRegMonth);
  result.year = ReadReg(kRegYear) + 2000;

  return result;
}

void RtcDevice::WriteTime(FidlRtc::wire::Time time) {
  std::scoped_lock lock(time_lock_);
  CheckRtcMode();

  WriteRegRaw(kRegB, ReadRegRaw(kRegB) | kRegBUpdateCycleInhibitBit);

  WriteReg(kRegSeconds, time.seconds);
  WriteReg(kRegMinutes, time.minutes);
  WriteHour(time.hours);

  WriteReg(kRegDayOfMonth, time.day);
  WriteReg(kRegMonth, time.month);
  // If present, we should use the "century" register described by the FADT.
  if (unlikely(time.year >= 2100)) {
    zxlogf(
        WARNING,
        "The Intel RTC driver does not support the year 2100. Please return to the 21st century.");
  }
  WriteReg(kRegYear, static_cast<uint8_t>(time.year - 2000));

  WriteRegRaw(kRegB, ReadRegRaw(kRegB) & ~kRegBUpdateCycleInhibitBit);
}

void RtcDevice::Get(GetRequestView request, GetCompleter::Sync& completer) {
  completer.Reply(ReadTime());
}

void RtcDevice::Set(SetRequestView request, SetCompleter::Sync& completer) {
  WriteTime(request->rtc);
  completer.Reply(ZX_OK);
}

zx_status_t Bind(void* ctx, zx_device_t* parent) {
  auto client = acpi::Client::Create(parent);
  if (client.is_error()) {
    return client.error_value();
  }
  auto acpi = std::move(client.value());

  auto pio = acpi.borrow().GetPio(0);
  if (!pio.ok() || pio->result.is_err()) {
    zxlogf(ERROR, "Failed to get port I/O resource");
    return pio.ok() ? pio->result.err() : pio.status();
  }

  zx::resource io_port = std::move(pio->result.mutable_response().pio);
  zx_info_resource_t resource_info;
  zx_status_t status =
      io_port.get_info(ZX_INFO_RESOURCE, &resource_info, sizeof(resource_info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "io_port.get_info failed: %d", status);
    return status;
  }

  if (resource_info.base > UINT16_MAX) {
    zxlogf(ERROR, "UART port base is too high.");
    return ZX_ERR_BAD_STATE;
  }
  if (resource_info.size < kPortCount) {
    zxlogf(ERROR, "Not enough I/O ports: wanted %u, got %lu", kPortCount, resource_info.size);
    return ZX_ERR_BAD_STATE;
  }

  status = zx_ioports_request(io_port.get(), resource_info.base, resource_info.size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_ioports_request_failed: %d", status);
    return status;
  }

  std::unique_ptr<RtcDevice> rtc = std::make_unique<RtcDevice>(
      parent, std::move(pio->result.mutable_response().pio), resource_info.base);
  auto time = rtc->ReadTime();
  auto new_time = rtc::SanitizeRtc(time);
  rtc->WriteTime(new_time);

  status = rtc->DdkAdd(ddk::DeviceAddArgs("rtc").set_proto_id(ZX_PROTOCOL_RTC));
  if (status == ZX_OK) {
    __UNUSED auto unused = rtc.release();
  }

  return status;
}

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Bind,
};
}  // namespace intel_rtc

ZIRCON_DRIVER(intel_rtc, intel_rtc::driver_ops, "zircon", "0.1");
