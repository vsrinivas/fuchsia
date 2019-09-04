// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/rtc/c/fidl.h>
#include <librtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <hw/inout.h>

#define RTC_IO_BASE 0x70
#define RTC_NUM_IO_REGISTERS 8

#define RTC_IDX_REG 0x70
#define RTC_DATA_REG 0x71

#define RTC_HOUR_PM_BIT 0x80

static mtx_t lock = MTX_INIT;

enum intel_rtc_registers {
  REG_SECONDS,
  REG_SECONDS_ALARM,
  REG_MINUTES,
  REG_MINUTES_ALARM,
  REG_HOURS,
  REG_HOURS_ALARM,
  REG_DAY_OF_WEEK,
  REG_DAY_OF_MONTH,
  REG_MONTH,
  REG_YEAR,
  REG_A,
  REG_B,
  REG_C,
  REG_D,
};

enum intel_rtc_register_a {
  REG_A_UPDATE_IN_PROGRESS_BIT = 1 << 7,
};

enum intel_rtc_register_b {
  REG_B_DAYLIGHT_SAVINGS_ENABLE_BIT = 1 << 0,
  REG_B_HOUR_FORMAT_BIT = 1 << 1,
  REG_B_DATA_MODE_BIT = 1 << 2,
  REG_B_SQUARE_WAVE_ENABLE_BIT = 1 << 3,
  REG_B_UPDATE_ENDED_INTERRUPT_ENABLE_BIT = 1 << 4,
  REB_B_ALARM_INTERRUPT_ENABLE_BIT = 1 << 5,
  REG_B_PERIODIC_INTERRUPT_ENABLE_BIT = 1 << 6,
  REG_B_UPDATE_CYCLE_INHIBIT_BIT = 1 << 7,
};

static uint8_t read_reg_raw(enum intel_rtc_registers reg) {
  outp(RTC_IDX_REG, reg);
  return inp(RTC_DATA_REG);
}

static void write_reg_raw(enum intel_rtc_registers reg, uint8_t val) {
  outp(RTC_IDX_REG, reg);
  outp(RTC_DATA_REG, val);
}

static uint8_t read_reg(enum intel_rtc_registers reg, bool reg_is_binary) {
  uint8_t data = read_reg_raw(reg);
  return reg_is_binary ? data : from_bcd(data);
}

static void write_reg(enum intel_rtc_registers reg, uint8_t val, bool reg_is_binary) {
  write_reg_raw(reg, reg_is_binary ? val : to_bcd(val));
}

// The high bit (RTC_HOUR_PM_BIT) is special for hours when not using
// the 24 hour time encoding. In that case, it is set for PM and unset
// for AM. This is true for both BCD and binary encodings of the
// value, so it has to be masked out first.

static uint8_t read_reg_hour(bool reg_is_binary, bool reg_is_24_hour) {
  uint8_t data = read_reg_raw(REG_HOURS);

  bool pm = data & RTC_HOUR_PM_BIT;
  data &= ~RTC_HOUR_PM_BIT;

  uint8_t hour = reg_is_binary ? data : from_bcd(data);

  if (reg_is_24_hour) {
    return hour;
  }

  if (pm) {
    hour += 12;
  }

  // Adjust noon and midnight.
  switch (hour) {
    case 24:  // 12 PM
      return 12;
    case 12:  // 12 AM
      return 0;
    default:
      return hour;
  }
}

static void write_reg_hour(uint8_t hour, bool reg_is_binary, bool reg_is_24_hour) {
  bool pm = hour > 11;

  if (!reg_is_24_hour) {
    if (pm) {
      hour -= 12;
    }
    if (hour == 0) {
      hour = 12;
    }
  }

  uint8_t data = reg_is_binary ? hour : to_bcd(hour);

  if (pm && !reg_is_24_hour) {
    data |= RTC_HOUR_PM_BIT;
  }

  write_reg_raw(REG_HOURS, data);
}

static zx_status_t set_utc_offset(const fuchsia_hardware_rtc_Time* rtc) {
  uint64_t rtc_nanoseconds = seconds_since_epoch(rtc) * 1000000000;
  int64_t offset = rtc_nanoseconds - zx_clock_get_monotonic();
  // Please do not use get_root_resource() in new code. See ZX-1467.
  return zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
}

// Retrieve the hour format and data mode bits. Note that on some
// platforms (including the acer) these bits can not be reliably
// written. So we must instead parse and provide the data in whatever
// format is given to us.
static void rtc_mode(bool* reg_is_24_hour, bool* reg_is_binary) {
  uint8_t reg_b = read_reg_raw(REG_B);
  *reg_is_24_hour = reg_b & REG_B_HOUR_FORMAT_BIT;
  *reg_is_binary = reg_b & REG_B_DATA_MODE_BIT;
}

static void read_time(fuchsia_hardware_rtc_Time* rtc) {
  mtx_lock(&lock);
  bool reg_is_24_hour;
  bool reg_is_binary;
  rtc_mode(&reg_is_24_hour, &reg_is_binary);

  rtc->seconds = read_reg(REG_SECONDS, reg_is_binary);
  rtc->minutes = read_reg(REG_MINUTES, reg_is_binary);
  rtc->hours = read_reg_hour(reg_is_binary, reg_is_24_hour);

  rtc->day = read_reg(REG_DAY_OF_MONTH, reg_is_binary);
  rtc->month = read_reg(REG_MONTH, reg_is_binary);
  rtc->year = read_reg(REG_YEAR, reg_is_binary) + 2000;

  mtx_unlock(&lock);
}

static void write_time(const fuchsia_hardware_rtc_Time* rtc) {
  mtx_lock(&lock);
  bool reg_is_24_hour;
  bool reg_is_binary;
  rtc_mode(&reg_is_24_hour, &reg_is_binary);

  write_reg_raw(REG_B, read_reg_raw(REG_B) | REG_B_UPDATE_CYCLE_INHIBIT_BIT);

  write_reg(REG_SECONDS, rtc->seconds, reg_is_binary);
  write_reg(REG_MINUTES, rtc->minutes, reg_is_binary);
  write_reg_hour(rtc->hours, reg_is_binary, reg_is_24_hour);

  write_reg(REG_DAY_OF_MONTH, rtc->day, reg_is_binary);
  write_reg(REG_MONTH, rtc->month, reg_is_binary);
  write_reg(REG_YEAR, rtc->year - 2000, reg_is_binary);

  write_reg_raw(REG_B, read_reg_raw(REG_B) & ~REG_B_UPDATE_CYCLE_INHIBIT_BIT);

  mtx_unlock(&lock);
}

static zx_status_t intel_rtc_get(void* ctx, fuchsia_hardware_rtc_Time* rtc) {
  // Ensure we have a consistent time.
  fuchsia_hardware_rtc_Time prev;
  do {
    // Using memcpy, as we use memcmp to compare.
    memcpy(&prev, rtc, sizeof prev);
    read_time(rtc);
  } while (memcmp(rtc, &prev, sizeof prev));
  return ZX_OK;
}

static zx_status_t intel_rtc_set(void* ctx, const fuchsia_hardware_rtc_Time* rtc) {
  // An invalid time was supplied.
  if (rtc_is_invalid(rtc)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  write_time(rtc);
  // TODO(kulakowski) This isn't the place for this long term.
  zx_status_t status = set_utc_offset(rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
  }
  return ZX_OK;
}

static zx_status_t fidl_Get(void* ctx, fidl_txn_t* txn) {
  fuchsia_hardware_rtc_Time rtc;
  intel_rtc_get(ctx, &rtc);
  return fuchsia_hardware_rtc_DeviceGet_reply(txn, &rtc);
}

static zx_status_t fidl_Set(void* ctx, const fuchsia_hardware_rtc_Time* rtc, fidl_txn_t* txn) {
  zx_status_t status = intel_rtc_set(ctx, rtc);
  return fuchsia_hardware_rtc_DeviceSet_reply(txn, status);
}

static fuchsia_hardware_rtc_Device_ops_t fidl_ops = {
    .Get = fidl_Get,
    .Set = fidl_Set,
};

static zx_status_t intel_rtc_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_rtc_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t intel_rtc_device_proto __UNUSED = {.version = DEVICE_OPS_VERSION,
                                                               .message = intel_rtc_message};

// TODO: bind against hw, not misc
static zx_status_t intel_rtc_bind(void* ctx, zx_device_t* parent) {
#if defined(__x86_64__) || defined(__i386__)
  // TODO(teisenbe): This should be probed via the ACPI pseudo bus whenever it
  // exists.

  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_ioports_request(get_root_resource(), RTC_IO_BASE, RTC_NUM_IO_REGISTERS);
  if (status != ZX_OK) {
    return status;
  }

  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = "rtc",
                            .ops = &intel_rtc_device_proto,
                            .proto_id = ZX_PROTOCOL_RTC};

  zx_device_t* dev;
  status = device_add(parent, &args, &dev);
  if (status != ZX_OK) {
    return status;
  }

  fuchsia_hardware_rtc_Time rtc;
  sanitize_rtc(NULL, &rtc, intel_rtc_get, intel_rtc_set);
  status = set_utc_offset(&rtc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "The RTC driver was unable to set the UTC clock!\n");
  }

  return ZX_OK;
#else
  return ZX_ERR_NOT_SUPPORTED;
#endif
}

static zx_driver_ops_t intel_rtc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = intel_rtc_bind,
};

ZIRCON_DRIVER_BEGIN(intel_rtc, intel_rtc_driver_ops, "zircon", "0.1", 6)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
    BI_GOTO_IF(NE, BIND_ACPI_HID_0_3, 0x504e5030, 0),  // PNP0B00\0
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x42303000), BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_ACPI_CID_0_3, 0x504e5030),  // PNP0B00\0
    BI_MATCH_IF(EQ, BIND_ACPI_CID_4_7, 0x42303000), ZIRCON_DRIVER_END(intel_rtc)
