// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zbi (//sdk/fidl/zbi/driver-config.fidl)
// by zither, a Fuchsia platform tool.

#ifndef SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_
#define SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

// ZBI_TYPE_KERNEL_DRIVER item types (for zbi_header_t.extra)
typedef uint32_t zbi_kernel_driver_t;

// 'PSCI'
#define ZBI_KERNEL_DRIVER_ARM_PSCI ((zbi_kernel_driver_t)(0x49435350u))

// 'GIC2'
#define ZBI_KERNEL_DRIVER_ARM_GIC_V2 ((zbi_kernel_driver_t)(0x32434947u))

// 'GIC3'
#define ZBI_KERNEL_DRIVER_ARM_GIC_V3 ((zbi_kernel_driver_t)(0x33434947u))

// 'ATIM'
#define ZBI_KERNEL_DRIVER_ARM_GENERIC_TIMER ((zbi_kernel_driver_t)(0x4d495441u))

// 'PL0U'
#define ZBI_KERNEL_DRIVER_PL011_UART ((zbi_kernel_driver_t)(0x55304c50u))

// 'AMLU'
#define ZBI_KERNEL_DRIVER_AMLOGIC_UART ((zbi_kernel_driver_t)(0x554c4d41u))

// 'AMLH'
#define ZBI_KERNEL_DRIVER_AMLOGIC_HDCP ((zbi_kernel_driver_t)(0x484c4d41u))

// 'DW8U'
#define ZBI_KERNEL_DRIVER_DW8250_UART ((zbi_kernel_driver_t)(0x44573855u))

// 'AMLR'
#define ZBI_KERNEL_DRIVER_AMLOGIC_RNG ((zbi_kernel_driver_t)(0x484c4d52u))

// 'WD32'
#define ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG ((zbi_kernel_driver_t)(0x32334457u))

// '8250'
#define ZBI_KERNEL_DRIVER_I8250_PIO_UART ((zbi_kernel_driver_t)(0x30353238u))

// '825M'
#define ZBI_KERNEL_DRIVER_I8250_MMIO_UART ((zbi_kernel_driver_t)(0x4d353238u))

// 'MMTU'
#define ZBI_KERNEL_DRIVER_MOTMOT_UART ((zbi_kernel_driver_t)(0x4d4d5455u))

// 'MMTP'
#define ZBI_KERNEL_DRIVER_MOTMOT_POWER ((zbi_kernel_driver_t)(0x4d4d5450u))

// '370P'
#define ZBI_KERNEL_DRIVER_AS370_POWER ((zbi_kernel_driver_t)(0x50303733u))

// Kernel driver struct that can be used for simple drivers.
// Used by ZBI_KERNEL_DRIVER_PL011_UART, ZBI_KERNEL_DRIVER_AMLOGIC_UART, and
// ZBI_KERNEL_DRIVER_I8250_MMIO_UART.
typedef struct {
  uint64_t mmio_phys;
  uint32_t irq;
  uint32_t reserved;
} zbi_dcfg_simple_t;

// Used by ZBI_KERNEL_DRIVER_I8250_PIO_UART.
typedef struct {
  uint16_t base;
  uint16_t reserved;
  uint32_t irq;
} zbi_dcfg_simple_pio_t;

// for ZBI_KERNEL_DRIVER_ARM_PSCI
typedef struct {
  uint8_t use_hvc;
  uint8_t reserved[7];
  uint64_t shutdown_args[3];
  uint64_t reboot_args[3];
  uint64_t reboot_bootloader_args[3];
  uint64_t reboot_recovery_args[3];
} zbi_dcfg_arm_psci_driver_t;

// for ZBI_KERNEL_DRIVER_ARM_GIC_V2
typedef struct {
  uint64_t mmio_phys;
  uint64_t msi_frame_phys;
  uint64_t gicd_offset;
  uint64_t gicc_offset;
  uint64_t gich_offset;
  uint64_t gicv_offset;
  uint32_t ipi_base;
  uint8_t optional;
  uint8_t use_msi;
  uint16_t reserved;
} zbi_dcfg_arm_gic_v2_driver_t;

// for ZBI_KERNEL_DRIVER_ARM_GIC_V3
typedef struct {
  uint64_t mmio_phys;
  uint64_t gicd_offset;
  uint64_t gicr_offset;
  uint64_t gicr_stride;
  uint64_t reserved0;
  uint32_t ipi_base;
  uint8_t optional;
  uint8_t reserved1[3];
} zbi_dcfg_arm_gic_v3_driver_t;

// for ZBI_KERNEL_DRIVER_ARM_GENERIC_TIMER
typedef struct {
  uint32_t irq_phys;
  uint32_t irq_virt;
  uint32_t irq_sphys;
  uint32_t freq_override;
} zbi_dcfg_arm_generic_timer_driver_t;

// for ZBI_KERNEL_DRIVER_AMLOGIC_HDCP
typedef struct {
  uint64_t preset_phys;
  uint64_t hiu_phys;
  uint64_t hdmitx_phys;
} zbi_dcfg_amlogic_hdcp_driver_t;

// for ZBI_KERNEL_DRIVER_AMLOGIC_RNG
typedef struct {
  uint64_t rng_data_phys;
  uint64_t rng_status_phys;
  uint64_t rng_refresh_interval_usec;
} zbi_dcfg_amlogic_rng_driver_t;

// Defines a register write action for a generic kernel watchdog driver.  An
// action consists of the following steps.
//
// 1) Read from the register located a physical address |addr|
// 2) Clear all of the bits in the value which was read using the |clr_mask|
// 3) Set all of the bits in the value using the |set_mask|
// 4) Write this value back to the address located at addr
typedef struct {
  uint64_t addr;
  uint32_t clr_mask;
  uint32_t set_mask;
} zbi_dcfg_generic32_watchdog_action_t;

typedef uint32_t zbi_kernel_driver_generic32_watchdog_flags_t;

#define ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG_FLAGS_ENABLED \
  ((zbi_kernel_driver_generic32_watchdog_flags_t)(1u << 0))

// 1ms
#define ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG_MIN_PERIOD ((int64_t)(1000000u))

// Definitions of actions which may be taken by a generic 32 bit watchdog timer
// kernel driver which may be passed by a bootloader.  Field definitions are as
// follows.
typedef struct {
  // The address and masks needed to "pet" (aka, dismiss) a hardware watchdog timer.
  zbi_dcfg_generic32_watchdog_action_t pet_action;

  // The address and masks needed to enable a hardware watchdog timer.  If enable
  // is an unsupported operation, the addr of the |enable_action| shall be zero.
  zbi_dcfg_generic32_watchdog_action_t enable_action;

  // The address and masks needed to disable a hardware watchdog timer.  If
  // disable is an unsupported operation, the addr of the |disable_action| shall
  // be zero.
  zbi_dcfg_generic32_watchdog_action_t disable_action;

  // The period of the watchdog timer given in nanoseconds.  When enabled, the
  // watchdog timer driver must pet the watch dog at least this often.  The value
  // must be at least 1 mSec, typically much larger (on the order of a second or
  // two).
  int64_t watchdog_period_nsec;

  // Storage for additional flags.  Currently, only one flag is defined,
  // "FLAG_ENABLED".  When this flag is set, it indicates that the watchdog timer
  // was left enabled by the bootloader at startup.
  zbi_kernel_driver_generic32_watchdog_flags_t flags;
  uint32_t reserved;
} zbi_dcfg_generic32_watchdog_t;

#if defined(__cplusplus)
}
#endif

#endif  // SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_
