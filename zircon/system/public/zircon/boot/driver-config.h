// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_
#define SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// ZBI_TYPE_KERNEL_DRIVER item types (for zbi_header_t.extra)
#define KDRV_ARM_PSCI 0x49435350                // 'PSCI'
#define KDRV_ARM_GIC_V2 0x32434947              // 'GIC2'
#define KDRV_ARM_GIC_V3 0x33434947              // 'GIC3'
#define KDRV_ARM_GENERIC_TIMER 0x4D495441       // 'ATIM'
#define KDRV_PL011_UART 0x55304C50              // 'PL0U'
#define KDRV_AMLOGIC_UART 0x554C4D41            // 'AMLU'
#define KDRV_NXP_IMX_UART 0x55584D49            // 'IMXU'
#define KDRV_MT8167_UART 0x5538544D             // 'MT8U'
#define KDRV_AMLOGIC_HDCP 0x484C4D41            // 'AMLH'
#define KDRV_MSM_UART 0x554D534D                // 'MSMU'
#define KDRV_MSM_POWER 1347244877               // 'MSMP'
#define KDRV_DW8250_UART 0x44573855             // 'DW8U'
#define KDRV_AS370_POWER 0x50303733             // '370P'
#define KDRV_AMLOGIC_RNG 0x484C4D52             // 'AMLR'
#define KDRV_GENERIC_32BIT_WATCHDOG 0x32334457  // 'WD32'
#define KDRV_I8250_PIO_UART 0x30353238          // '8250'
#define KDRV_I8250_MMIO_UART 0x4d353238         // '825M'

// Kernel driver struct that can be used for simple drivers.
// Used by KDRV_PL011_UART, KDRV_AMLOGIC_UART, KDRV_NXP_IMX_UART,
// and KDRV_I8250_MMIO_UART.
typedef struct {
  uint64_t mmio_phys;
  uint32_t irq;
} dcfg_simple_t;

// Used by KDRV_I8250_PIO_UART.
typedef struct {
  uint16_t base;
  uint32_t irq;
} dcfg_simple_pio_t;

// for KDRV_MT8167_UART
typedef struct {
  uint64_t soc_mmio_phys;
  uint64_t uart_mmio_phys;
  uint32_t irq;
} dcfg_soc_uart_t;

// for KDRV_ARM_PSCI
typedef struct {
  bool use_hvc;
  uint64_t shutdown_args[3];
  uint64_t reboot_args[3];
  uint64_t reboot_bootloader_args[3];
  uint64_t reboot_recovery_args[3];
} dcfg_arm_psci_driver_t;

typedef struct {
  uint64_t soc_imem_phys;
  uint64_t soc_imem_offset;
} dcfg_msm_power_driver_t;

// for KDRV_ARM_GIC_V2
typedef struct {
  uint64_t mmio_phys;
  uint64_t msi_frame_phys;
  uint64_t gicd_offset;
  uint64_t gicc_offset;
  uint64_t gich_offset;
  uint64_t gicv_offset;
  uint32_t ipi_base;
  bool optional;
  bool use_msi;
} dcfg_arm_gicv2_driver_t;

// for KDRV_ARM_GIC_V3
typedef struct {
  uint64_t mmio_phys;
  uint64_t gicd_offset;
  uint64_t gicr_offset;
  uint64_t gicr_stride;
  uint64_t mx8_gpr_phys;
  uint32_t ipi_base;
  bool optional;
} dcfg_arm_gicv3_driver_t;

// for KDRV_ARM_GENERIC_TIMER
typedef struct {
  uint32_t irq_phys;
  uint32_t irq_virt;
  uint32_t irq_sphys;
  uint32_t freq_override;
} dcfg_arm_generic_timer_driver_t;

// for KDRV_AMLOGIC_HDCP
typedef struct {
  uint64_t preset_phys;
  uint64_t hiu_phys;
  uint64_t hdmitx_phys;
} dcfg_amlogic_hdcp_driver_t;

// for KDRV_AMLOGIC_RNG
typedef struct {
  uint64_t rng_data_phys;
  uint64_t rng_status_phys;
  uint64_t rng_refresh_interval_usec;
} dcfg_amlogic_rng_driver_t;

// Defines a register write action for a generic kernel watchdog driver.  An
// action consists of the following steps.
//
// 1) Read from the register located a physical address |addr|
// 2) Clear all of the bits in the value which was read using the |clr_mask|
// 3) Set all of the bits in the value using the |set_mask|
// 4) Write this value back to the address located at addr.
//
typedef struct {
  uint64_t addr;
  uint32_t clr_mask;
  uint32_t set_mask;
} dcfg_generic_32bit_watchdog_action_t;

#define KDRV_GENERIC_32BIT_WATCHDOG_FLAG_ENABLED ((uint32_t)0x00000001)
#define KDRV_GENERIC_32BIT_WATCHDOG_MIN_PERIOD ZX_MSEC(1)

// Definitions of actions which may be taken by a generic 32 bit watchdog timer
// kernel driver which may be passed by a bootloader.  Field definitions are as
// follows.
//
// |pet_action|
// The address and masks needed to "pet" (aka, dismiss) a hardware watchdog timer.
//
// |enable_action|
// The address and masks needed to enable a hardware watchdog timer.  If enable
// is an unsupported operation, the addr of the |enable_action| shall be zero.
//
// |disable_action|
// The address and masks needed to disable a hardware watchdog timer.  If
// disable is an unsupported operation, the addr of the |disable_action| shall
// be zero.
//
// |watchdog_period_nsec|
// The period of the watchdog timer given in nanoseconds.  When enabled, the
// watchdog timer driver must pet the watch dog at least this often.  The value
// must be at least 1 mSec, typically much larger (on the order of a second or
// two)
//
// |flags|
// Storage for additional flags.  Currently, only one flag is defined,
// "FLAG_ENABLED".  When this flag is set, it indicates that the watchdog timer
// was left enabled by the bootloader at startup.
typedef struct {
  dcfg_generic_32bit_watchdog_action_t pet_action;
  dcfg_generic_32bit_watchdog_action_t enable_action;
  dcfg_generic_32bit_watchdog_action_t disable_action;
  zx_duration_t watchdog_period_nsec;
  uint32_t flags;
} dcfg_generic_32bit_watchdog_t;

#endif  // SYSROOT_ZIRCON_BOOT_DRIVER_CONFIG_H_
