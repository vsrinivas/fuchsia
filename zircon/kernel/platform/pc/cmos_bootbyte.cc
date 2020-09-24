// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <limits.h>
#include <stdint.h>

#include <arch/x86.h>
#include <platform/pc/bootbyte.h>

#define RTC_BASE_PORT (uint16_t)0x70

#define RTC_BOOT_BYTE 48  // CMOS register offset

// flags and fields in RTC_BOOT_BYTE
#define RTC_BOOT_NORMAL 0x1u         // boot_option
#define RTC_BOOT_RECOVERY 0x2u       // boot_option
#define RTC_BOOT_BOOTLOADER 0x4u     // boot_option
#define RTC_BOOT_COUNT_MASK (0xf0u)  // reboot_counter field mask
#define RTC_BOOT_COUNT_SHIFT (4)     // reboot_counter shift amount

#define RTC_BOOT_COUNT_INITIAL (uint8_t)3  // reboot_counter initial value

static void cmos_write(uint8_t addr, uint8_t val) {
  uint16_t ofs;

  if (addr < 128) {
    ofs = 0;
  } else {
    ofs = 2;
    addr = (uint8_t)(addr - 128);
  }

  outp((uint16_t)(RTC_BASE_PORT + ofs), addr);
  outp((uint16_t)(RTC_BASE_PORT + ofs + 1), val);
}

void bootbyte_set_reason(uint64_t reason) {
  uint8_t val;

  // set boot reason, clamp to be in range of a uint8_t, default to RTC_BOOT_NORMAL
  if (reason <= 255) {
    val = (uint8_t)reason;
  } else {
    val = RTC_BOOT_NORMAL;
  }

  // set default number of boot attempts
  val |= (RTC_BOOT_COUNT_INITIAL << RTC_BOOT_COUNT_SHIFT);
  cmos_write(RTC_BOOT_BYTE, val);  // boot_option and reboot_counter
}
