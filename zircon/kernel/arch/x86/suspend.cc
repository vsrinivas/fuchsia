// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/x86/suspend.h"

#include <bits.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <pow2.h>
#include <trace.h>

#include <cstddef>
#include <cstdint>

#include <arch/ops.h>
#include <arch/x86.h>
#include <platform/pc/acpi.h>

namespace {

// PM1 control register constants. See ACPI v6.3 Section 4.8.3.2.1
constexpr uint8_t kBitpositionSleepType = 0x0A;
constexpr uint16_t kBitmaskSleepType = 0x1C00;        // Bits 10-12
constexpr uint16_t kBitmaskSleepEnable = 0x2000;      // Bit 13
constexpr uint32_t kBitmaskPm1CntWriteonly = 0x2004;  // Bits 2, 13

// PM1 status register constants. See ACPI v6.3 Section 4.8.3.1.1
constexpr uint8_t kBitpositionWakeStatus = 0x0F;
constexpr uint16_t kBitmaskWakeStatus = 0x8000;  // Bit 15

constexpr uint8_t kAdrSpaceSystemIo = 1;
constexpr uint8_t kMaxIoBitWidth = 32;
constexpr size_t kMaxIoPort = UINT16_MAX;

// Assumes IO port address space which has a maximum access bit width of 32. Assumes the GAS format
// used by FADT which ignores access_size and instead uses register_bit_width to determine the
// access bit width. Although this behaviour is not in documentation, it can be observed in
// hardware.
uint8_t get_access_bit_width(const acpi_lite::AcpiGenericAddress* reg) {
  return (reg->register_bit_width < kMaxIoBitWidth) ? reg->register_bit_width : kMaxIoBitWidth;
}

// Check that the register uses IO port address space and is valid for the GAS format used by the
// FADT.
zx_status_t validate_register(const acpi_lite::AcpiGenericAddress* reg) {
  if (!reg->address) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (reg->address_space_id != kAdrSpaceSystemIo) {
    TRACEF("Unsupported register address space: %d.", reg->address_space_id);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Validate that the register is in the legacy GAS format used by the FADT, indicated by a
  // bit_offset of 0 and register_bit_width of 8/16/32/64. Although this behaviour is not in
  // documentation, it can be observed in hardware.
  if (reg->register_bit_offset != 0 || !ispow2(reg->register_bit_width) ||
      reg->register_bit_width < 8 || reg->register_bit_width > 64) {
    TRACEF("Register is not in the GAS format used by the FADT.");
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t read_io_port(uint64_t address, uint32_t width, uint32_t* value) {
  if (address > kMaxIoPort) {
    TRACEF("Unable to read IO port. Requested address (%lu) greater than maximum IO port.",
           address);
    return ZX_ERR_INVALID_ARGS;
  }

  const uint16_t io_port = static_cast<uint16_t>(address);

  switch (width) {
    case 8:
      *value = inp(io_port);
      break;
    case 16:
      *value = inpw(io_port);
      break;
    case 32:
      *value = inpd(io_port);
      break;
    default:
      TRACEF("Unable to read IO port. Invalid width requested: %u.", width);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t write_io_port(uint64_t address, uint32_t width, uint32_t value) {
  if (address > kMaxIoPort) {
    TRACEF("Unable to write IO port. Invalid address: %lu.", address);
    return ZX_ERR_INVALID_ARGS;
  }

  const uint16_t io_port = static_cast<uint16_t>(address);

  switch (width) {
    case 8:
      outp(io_port, static_cast<uint8_t>(value));
      break;
    case 16:
      outpw(io_port, static_cast<uint16_t>(value));
      break;
    case 32:
      outpd(io_port, static_cast<uint32_t>(value));
      break;
    default:
      TRACEF("Unable to write IO port. Invalid width requested: %u.", width);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t read_register(const acpi_lite::AcpiGenericAddress* reg, uint64_t* value) {
  zx_status_t status = validate_register(reg);
  if (status != ZX_OK) {
    return status;
  }

  *value = 0;
  const uint8_t access_width = get_access_bit_width(reg);
  uint32_t bits_to_read = reg->register_bit_width;
  uint32_t ioport_read_value;
  uint8_t index = 0;

  // Read |bits_to_read| bits from |reg->address| in chunks of |access_width| bits.
  while (bits_to_read) {
    status = read_io_port(
        reg->address + static_cast<uint64_t>(index * static_cast<uint32_t>(access_width >> 3)),
        access_width, &ioport_read_value);
    if (status != ZX_OK) {
      return status;
    }
    *value |= (static_cast<uint64_t>(ioport_read_value) & BIT_MASK(access_width))
              << (index * access_width);

    bits_to_read -= access_width;
    index++;
  }
  return ZX_OK;
}

zx_status_t write_register(const acpi_lite::AcpiGenericAddress* reg, uint64_t value) {
  zx_status_t status = validate_register(reg);
  if (status != ZX_OK) {
    return status;
  }

  const uint8_t access_width = get_access_bit_width(reg);
  uint32_t bits_to_write = reg->register_bit_width;
  uint8_t index = 0;

  // Write |bits_to_write| bits to |reg->address| in chunks of |access_width| bits.
  while (bits_to_write) {
    // |access_width| is 32 at most so we can safely cast to uint32_t
    const uint32_t write_bits =
        static_cast<uint32_t>((value >> (index * access_width)) & BIT_MASK(access_width));
    status = write_io_port(
        reg->address + static_cast<uint64_t>(index * static_cast<uint32_t>(access_width >> 3)),
        access_width, write_bits);
    if (status != ZX_OK) {
      return status;
    }

    bits_to_write -= access_width;
    index++;
  }

  return status;
}

zx_status_t read_ab_register(const acpi_lite::AcpiGenericAddress* reg_a,
                             const acpi_lite::AcpiGenericAddress* reg_b, uint64_t* value) {
  uint64_t value_a = 0;
  zx_status_t status = read_register(reg_a, &value_a);
  if (status != ZX_OK) {
    return status;
  }

  uint64_t value_b = 0;
  if (reg_b->address) {
    status = read_register(reg_b, &value_b);
    if (status != ZX_OK) {
      return status;
    }
  }
  *value = value_a | value_b;
  return ZX_OK;
}

zx_status_t write_ab_register(const acpi_lite::AcpiGenericAddress* reg_a,
                              const acpi_lite::AcpiGenericAddress* reg_b, uint64_t value_a,
                              uint64_t value_b) {
  zx_status_t status = write_register(reg_a, value_a);
  if (status != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }

  if (reg_b->address) {
    status = write_register(reg_b, value_b);
    if (status != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }
  }
  return ZX_OK;
}

}  // namespace

zx_status_t set_suspend_registers(uint8_t sleep_state, uint8_t sleep_type_a, uint8_t sleep_type_b) {
  ASSERT(arch_ints_disabled());

  const acpi_lite::AcpiFadt* acpi_fadt =
      acpi_lite::GetTableByType<acpi_lite::AcpiFadt>(GlobalAcpiLiteParser());
  if (acpi_fadt == nullptr) {
    TRACEF("Failed to get FADT.");
    return ZX_ERR_INTERNAL;
  }

  // Read PM1 control register
  uint64_t pm1a_control = 0;
  zx_status_t status =
      read_ab_register(&acpi_fadt->x_pm1a_cnt_blk, &acpi_fadt->x_pm1b_cnt_blk, &pm1a_control);
  if (status != ZX_OK) {
    TRACEF("Failed to read PM1 control register.");
    return ZX_ERR_INTERNAL;
  }

  // Clear the write-only bits
  pm1a_control &= ~kBitmaskPm1CntWriteonly;
  // Clear the sleep type and sleep enable bits
  pm1a_control &= ~(kBitmaskSleepType | kBitmaskSleepEnable);
  // Set the sleep type bits and write them to the registers
  uint64_t pm1b_control = pm1a_control;
  pm1a_control |= (sleep_type_a << kBitpositionSleepType);
  pm1b_control |= (sleep_type_b << kBitpositionSleepType);

  status = write_ab_register(&acpi_fadt->x_pm1a_cnt_blk, &acpi_fadt->x_pm1b_cnt_blk, pm1a_control,
                             pm1b_control);
  if (status != ZX_OK) {
    TRACEF("Failed to write sleep type to PM1 control register.");
    return ZX_ERR_INTERNAL;
  }

  // Flush CPU cache
  __asm__ volatile("wbinvd" : : : "memory");

  // Add the sleep enable bit and write to the registers
  pm1a_control |= kBitmaskSleepEnable;
  pm1b_control |= kBitmaskSleepEnable;

  status = write_ab_register(&acpi_fadt->x_pm1a_cnt_blk, &acpi_fadt->x_pm1b_cnt_blk, pm1a_control,
                             pm1b_control);
  if (status != ZX_OK) {
    TRACEF("Failed to write sleep type and sleep enable to PM1 control register.");
    return ZX_ERR_INTERNAL;
  }

  // Wait for resume
  uint64_t wake_status = 0;
  do {
    status = read_ab_register(&acpi_fadt->x_pm1a_evt_blk, &acpi_fadt->x_pm1b_evt_blk, &wake_status);
    if (status != ZX_OK) {
      TRACEF("Failed to read wake status from PM1 event register.");
      return ZX_ERR_INTERNAL;
    }
    wake_status = ((wake_status & kBitmaskWakeStatus) >> kBitpositionWakeStatus);
  } while (!wake_status);

  return ZX_OK;
}
