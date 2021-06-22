// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_

#include <zircon/compiler.h>

__BEGIN_CDECLS

// flags and fields in RTC_BOOT_BYTE
#define RTC_BOOT_NORMAL 0x1u
#define RTC_BOOT_RECOVERY 0x2u
#define RTC_BOOT_BOOTLOADER 0x4u
#define RTC_BOOT_DEFAULT 0xFFu
#define RTC_BOOT_COUNT_MASK (0xf0u)  // reboot_counter field mask

#ifdef GIGABOOT_HOST

// Fake bootbyte implementation for host tests.
// Make sure to call bootbyte_clear() after each test to avoid leaking state.
unsigned char bootbyte_read(void);
void bootbyte_clear(void);
void bootbyte_set_normal(void);
void bootbyte_set_recovery(void);
void bootbyte_set_bootloader(void);

// Extra test-only API.
void bootbyte_set_for_test(unsigned char value);

#else

#include <stdint.h>

// CMOS I/O port
#define RTC_BASE_PORT 0x70u

// CMOS register offset
#define RTC_BOOT_BYTE 48

// TODO(jonmayo): use a platform API instead of this hack
#if defined(__x86_64__)
static inline uint8_t inp(uint16_t _port) {
  uint8_t rv;
  __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
  return (rv);
}

static inline void outp(uint16_t _port, uint8_t _data) {
  __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
}

static inline void rtc_write(unsigned char addr, unsigned char value) {
  uint16_t ofs;

  if (addr < 128) {
    ofs = 0;
  } else {
    ofs = 2;
    addr -= 128;
  }

  outp(RTC_BASE_PORT + ofs, addr);
  outp(RTC_BASE_PORT + ofs + 1, value);
}

static inline unsigned char rtc_read(unsigned char addr) {
  uint16_t ofs;

  if (addr < 128) {
    ofs = 0;
  } else {
    ofs = 2;
    addr -= 128;
  }

  outp(RTC_BASE_PORT + ofs, addr);
  return inp(RTC_BASE_PORT + ofs + 1);
}

static inline unsigned char bootbyte_read(void) { return rtc_read(RTC_BOOT_BYTE); }

static inline void bootbyte_clear(void) { rtc_write(RTC_BOOT_BYTE, RTC_BOOT_DEFAULT); }

static inline void bootbyte_set_normal(void) { rtc_write(RTC_BOOT_BYTE, RTC_BOOT_NORMAL); }

static inline void bootbyte_set_recovery(void) { rtc_write(RTC_BOOT_BYTE, RTC_BOOT_RECOVERY); }

static inline void bootbyte_set_bootloader(void) { rtc_write(RTC_BOOT_BYTE, RTC_BOOT_BOOTLOADER); }

#elif defined(__aarch64__)
// TODO(jonmayo): add support for aarch64. currently stubbed out with dummy behavior

static inline unsigned char bootbyte_read(void) { return 0xff; }

static inline void bootbyte_clear(void) {}

static inline void bootbyte_set_normal(void) {}

static inline void bootbyte_set_recovery(void) {}

static inline void bootbyte_set_bootloader(void) {}

#else

#error "add code for other arches here"

#endif

#endif  // GIGABOOT_HOST

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_BOOTBYTE_H_
