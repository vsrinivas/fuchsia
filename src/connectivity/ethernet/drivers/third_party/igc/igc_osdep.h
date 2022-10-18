/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * $FreeBSD$
 */

// clang-format off
#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_OSDEP_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_OSDEP_H_

#include <assert.h>
#include <fuchsia/hardware/pci/c/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/inout.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/io-buffer.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio-buffer.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define ASSERT(x) assert(x)

#define nsec_delay(x) zx_nanosleep(zx_deadline_after(x))
#define usec_delay(x) nsec_delay(ZX_USEC(x))
#define usec_delay_irq(x) nsec_delay(ZX_USEC(x))
#define msec_delay(x) nsec_delay(ZX_MSEC(x))
#define msec_delay_irq(x) nsec_delay(ZX_MSEC(x))

#define DEBUGOUT(format, ...) zxlogf(DEBUG, "%s %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define DEBUGOUT1(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT2(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT3(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT7(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGFUNC(F) DEBUGOUT(F "\n")

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

#define __le16 u16
#define __le32 u32
#define __le64 u64

struct igc_osdep {
  ddk::Pci pci;
  uintptr_t membase;
  uintptr_t iobase;
  uintptr_t flashbase;
};

#define hw2pci(hw) (&((struct igc_osdep *)(hw)->back)->pci)
#define hw2membase(hw) (((struct igc_osdep *)(hw)->back)->membase)
#define hw2iobase(hw) (((struct igc_osdep *)(hw)->back)->iobase)
#define hw2flashbase(hw) (((struct igc_osdep *)(hw)->back)->flashbase)

#define igc_writeb(v, a) MmioWrite8((v), (MMIO_PTR uint8_t volatile *)(uintptr_t)(a))
#define igc_writew(v, a) MmioWrite16((v), (MMIO_PTR uint16_t volatile *)(uintptr_t)(a))
#define igc_writel(v, a) MmioWrite32((v), (MMIO_PTR uint32_t volatile *)(uintptr_t)(a))
#define igc_writell(v, a) MmioWrite64((v), (MMIO_PTR uint64_t volatile *)(uintptr_t)(a))

#define igc_readb(a) MmioRead8((MMIO_PTR uint8_t const volatile *)(uintptr_t)(a))
#define igc_readw(a) MmioRead16((MMIO_PTR uint16_t const volatile *)(uintptr_t)(a))
#define igc_readl(a) MmioRead32((MMIO_PTR uint32_t const volatile *)(uintptr_t)(a))
#define igc_readll(a) MmioRead64((MMIO_PTR uint64_t const volatile *)(uintptr_t)(a))

#define IGC_REGISTER(hw, reg) (u32) reg

#define IGC_WRITE_FLUSH(a) IGC_READ_REG(a, IGC_STATUS)

/* Read from an absolute offset in the adapter's memory space */
#define IGC_READ_OFFSET(hw, offset) igc_readl(hw2membase(hw) + (offset))

/* Write to an absolute offset in the adapter's memory space */
#define IGC_WRITE_OFFSET(hw, offset, value) igc_writel((value), hw2membase(hw) + (offset))

/* Register READ/WRITE macros */

#define IGC_READ_REG(hw, reg) IGC_READ_OFFSET((hw), IGC_REGISTER((hw), (reg)))

#define IGC_WRITE_REG(hw, reg, value) IGC_WRITE_OFFSET((hw), IGC_REGISTER((hw), (reg)), (value))

#define IGC_READ_REG_ARRAY(hw, reg, index) \
  IGC_READ_OFFSET((hw), IGC_REGISTER((hw), (reg)) + ((index) << 2))

#define IGC_WRITE_REG_ARRAY(hw, reg, index, value) \
  IGC_WRITE_OFFSET((hw), IGC_REGISTER((hw), (reg)) + ((index) << 2), (value))

#define IGC_READ_REG_ARRAY_DWORD IGC_READ_REG_ARRAY
#define IGC_WRITE_REG_ARRAY_DWORD IGC_WRITE_REG_ARRAY

#define IGC_READ_REG_ARRAY_BYTE(hw, reg, index) \
  igc_readb(hw2membase(hw) + IGC_REGISTER((hw), (reg)) + (index))

#define IGC_WRITE_REG_ARRAY_BYTE(hw, reg, index, value) \
  igc_writeb((value), hw2membase(hw) + IGC_REGISTER((hw), (reg)) + (index))

#define IGC_WRITE_REG_ARRAY_WORD(hw, reg, index, value) \
  igc_writew((value), hw2membase(hw) + IGC_REGISTER((hw), (reg)) + ((index) << 1))

#endif // SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_OSDEP_H_
// clang-format on
