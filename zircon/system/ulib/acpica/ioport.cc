// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/hw/inout.h>
#include <lib/pci/pio.h>

#include <acpica/acpi.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "zircon/system/ulib/acpica/osfuchsia.h"

#ifdef __x86_64__
// Essentially, we're using a bitmap here to represent each individual I/O port, so that we can
// keep track of which I/O ports are allowed and which are not by the kernel.

static constexpr size_t max_io_port = UINT16_MAX;
static constexpr size_t io_port_bitmap_size = max_io_port + 1;
static fbl::Mutex bitmap_lock;
static bitmap::RawBitmapGeneric<bitmap::FixedStorage<io_port_bitmap_size>> port_bitmap;

static void initialize_port_bitmap() {
  // This cannot fail given that we're using fixed storage
  port_bitmap.Reset(io_port_bitmap_size);
}

static bool check_port_permissions(uint16_t address, uint8_t width_bytes) {
  LTRACEF("Testing %#x until %#x, in bitmap of size %#zx\n", address, address + width_bytes,
          port_bitmap.size());

  return port_bitmap.Scan(address, address + width_bytes, true);
}

/**
 * @brief Make the I/O ports accessible and set them in the bitmap, so that we don't call
 * the kernel again.
 *
 * @param address The I/O address.
 * @param width_bytes The width of the access, in bytes.
 *
 * @return Status code that indicates success or reason for error.
 */
static zx_status_t add_port_permissions(uint16_t address, uint8_t width_bytes) {
  zx_status_t result = port_bitmap.Set(address, address + width_bytes);
  ZX_ASSERT(result == ZX_OK);

  LTRACEF("Adding permissions to [%#x, %#x]\n", address, address + width_bytes);

  return zx_ioports_request(root_resource_handle, address, width_bytes);
}

/**
 * @brief Handle all matters of I/O port permissions with the kernel.
 *
 * @param address The I/O address.
 * @param width_bits The width of the access, in bits.
 *
 * @return Status code that indicates success or reason for error.
 */
static zx_status_t handle_port_permissions(uint16_t address, UINT32 width_bits) {
  // It's a good idea to convert bits to bytes here, considering each
  // I/O port "byte" has its own bit in the bitmap
  uint8_t width_bytes = static_cast<uint8_t>(width_bits / 8);

  fbl::AutoLock g{&bitmap_lock};

  if (!check_port_permissions(address, width_bytes)) {
    // If the port is disallowed at the moment, call the kernel so it isn't
    return add_port_permissions(address, width_bytes);
  } else {
    LTRACEF("port %#x(width %#x) was already set.\n", address, width_bytes);
  }

  return ZX_OK;
}

static ACPI_STATUS zx_status_to_acpi_status(zx_status_t st) {
  // Note: This function was written with regard to zx_ioports_request(),
  // but it may be a good idea to fill this out with more ZX_ statuses
  // if needed in the future.
  switch (st) {
    case ZX_ERR_NO_MEMORY:
      return AE_NO_MEMORY;
    case ZX_ERR_ACCESS_DENIED:
      return AE_ACCESS;
    case ZX_ERR_INVALID_ARGS:
      return AE_BAD_PARAMETER;
    case ZX_OK:
      return AE_OK;
    default:
      return AE_ERROR;
  }
}

/**
 * @brief Read a value from an input port.
 *
 * @param Address Hardware I/O port address to be read.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The port width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32* Value, UINT32 Width) {
  if (Address > max_io_port) {
    return AE_BAD_PARAMETER;
  }

  uint16_t io_port = (uint16_t)Address;

  if (zx_status_t st = handle_port_permissions(io_port, Width); st != ZX_OK) {
    return zx_status_to_acpi_status(st);
  }

  switch (Width) {
    case 8:
      *Value = inp(io_port);
      break;
    case 16:
      *Value = inpw(io_port);
      break;
    case 32:
      *Value = inpd(io_port);
      break;
    default:
      return AE_BAD_PARAMETER;
  }
  return AE_OK;
}

/**
 * @brief Write a value to an output port.
 *
 * @param Address Hardware I/O port address where data is to be written.
 * @param Value The value to be written.
 * @param Width The port width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width) {
  if (Address > max_io_port) {
    return AE_BAD_PARAMETER;
  }

  uint16_t io_port = (uint16_t)Address;

  if (zx_status_t st = handle_port_permissions(io_port, Width); st != ZX_OK) {
    return zx_status_to_acpi_status(st);
  }

  switch (Width) {
    case 8:
      outp(io_port, (uint8_t)Value);
      break;
    case 16:
      outpw(io_port, (uint16_t)Value);
      break;
    case 32:
      outpd(io_port, (uint32_t)Value);
      break;
    default:
      return AE_BAD_PARAMETER;
  }
  return AE_OK;
}

ACPI_STATUS AcpiIoPortSetup() {
  initialize_port_bitmap();

  // For AcpiOsWritePort and AcpiOsReadPort to operate they need access to ioports 0xCF8 and 0xCFC
  // per the Pci Local Bus specification v3.0. Each address is a 32 bit port.
  for (const auto addr : {kPciConfigAddrPort, kPciConfigDataPort}) {
    zx_status_t pio_status = handle_port_permissions(addr, 32);
    if (pio_status != ZX_OK) {
      return zx_status_to_acpi_status(pio_status);
    }
  }
  return AE_OK;
}

#elif defined(__aarch64__)

ACPI_STATUS AcpiIoPortSetup() { return AE_OK; }
ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32* Value, UINT32 Width) {
  return AE_NOT_IMPLEMENTED;
}
ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width) {
  return AE_NOT_IMPLEMENTED;
}

#endif
