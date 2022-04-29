// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/resources.h"

#include <fidl/fuchsia.hardware.i2c.businfo/cpp/wire.h>
#include <fidl/fuchsia.hardware.spi.businfo/cpp/wire.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/status.h"

bool resource_is_memory(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_MEMORY24 || res->Type == ACPI_RESOURCE_TYPE_MEMORY32 ||
         res->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32;
}

bool resource_is_address(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_ADDRESS16 || res->Type == ACPI_RESOURCE_TYPE_ADDRESS32 ||
         res->Type == ACPI_RESOURCE_TYPE_ADDRESS64 ||
         res->Type == ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64;
}

bool resource_is_io(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_IO || res->Type == ACPI_RESOURCE_TYPE_FIXED_IO;
}

bool resource_is_irq(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_IRQ || res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
}

bool resource_is_spi(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
         res->Data.CommonSerialBus.Type == ACPI_RESOURCE_SERIAL_TYPE_SPI;
}

bool resource_is_i2c(ACPI_RESOURCE* res) {
  return res->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
         res->Data.CommonSerialBus.Type == ACPI_RESOURCE_SERIAL_TYPE_I2C;
}

zx_status_t resource_parse_memory(ACPI_RESOURCE* res, resource_memory_t* out) {
  switch (res->Type) {
    case ACPI_RESOURCE_TYPE_MEMORY24: {
      ACPI_RESOURCE_MEMORY24* m24 = &res->Data.Memory24;
      out->writeable = !m24->WriteProtect;
      out->minimum = (uint32_t)m24->Minimum << 8;
      out->maximum = (uint32_t)m24->Maximum << 8;
      out->alignment = m24->Alignment ? m24->Alignment : 1U << 16;
      out->address_length = (uint32_t)m24->AddressLength << 8;
      break;
    }
    case ACPI_RESOURCE_TYPE_MEMORY32: {
      ACPI_RESOURCE_MEMORY32* m32 = &res->Data.Memory32;
      out->writeable = !m32->WriteProtect;
      out->minimum = m32->Minimum;
      out->maximum = m32->Maximum;
      out->alignment = m32->Alignment;
      out->address_length = m32->AddressLength;
      break;
    }
    case ACPI_RESOURCE_TYPE_FIXED_MEMORY32: {
      ACPI_RESOURCE_FIXED_MEMORY32* m32 = &res->Data.FixedMemory32;
      out->writeable = !m32->WriteProtect;
      out->minimum = m32->Address;
      out->maximum = m32->Address;
      out->alignment = 1;
      out->address_length = m32->AddressLength;
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

#define EXTRACT_ADDRESS_FIELDS(src, out)                               \
  do {                                                                 \
    (out)->minimum = (src)->Address.Minimum;                           \
    (out)->maximum = (src)->Address.Maximum;                           \
    (out)->address_length = (src)->Address.AddressLength;              \
    (out)->translation_offset = (src)->Address.TranslationOffset;      \
    (out)->granularity = (src)->Address.Granularity;                   \
    (out)->consumed_only = ((src)->ProducerConsumer == ACPI_CONSUMER); \
    (out)->subtractive_decode = ((src)->Decode == ACPI_SUB_DECODE);    \
    (out)->min_address_fixed = (src)->MinAddressFixed;                 \
    (out)->max_address_fixed = (src)->MaxAddressFixed;                 \
  } while (0)

zx_status_t resource_parse_address(ACPI_RESOURCE* res, resource_address_t* out) {
  uint8_t resource_type;
  switch (res->Type) {
    case ACPI_RESOURCE_TYPE_ADDRESS16: {
      ACPI_RESOURCE_ADDRESS16* a16 = &res->Data.Address16;
      EXTRACT_ADDRESS_FIELDS(a16, out);
      resource_type = a16->ResourceType;
      break;
    }
    case ACPI_RESOURCE_TYPE_ADDRESS32: {
      ACPI_RESOURCE_ADDRESS32* a32 = &res->Data.Address32;
      EXTRACT_ADDRESS_FIELDS(a32, out);
      resource_type = a32->ResourceType;
      break;
    }
    case ACPI_RESOURCE_TYPE_ADDRESS64: {
      ACPI_RESOURCE_ADDRESS64* a64 = &res->Data.Address64;
      EXTRACT_ADDRESS_FIELDS(a64, out);
      resource_type = a64->ResourceType;
      break;
    }
    case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64: {
      ACPI_RESOURCE_EXTENDED_ADDRESS64* a64 = &res->Data.ExtAddress64;
      EXTRACT_ADDRESS_FIELDS(a64, out);
      resource_type = a64->ResourceType;
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  switch (resource_type) {
    case ACPI_MEMORY_RANGE:
      out->resource_type = RESOURCE_ADDRESS_MEMORY;
      break;
    case ACPI_IO_RANGE:
      out->resource_type = RESOURCE_ADDRESS_IO;
      break;
    case ACPI_BUS_NUMBER_RANGE:
      out->resource_type = RESOURCE_ADDRESS_BUS_NUMBER;
      break;
    default:
      out->resource_type = RESOURCE_ADDRESS_UNKNOWN;
  }

  return ZX_OK;
}

zx_status_t resource_parse_io(ACPI_RESOURCE* res, resource_io_t* out) {
  switch (res->Type) {
    case ACPI_RESOURCE_TYPE_IO: {
      ACPI_RESOURCE_IO* io = &res->Data.Io;
      out->decodes_full_space = (io->IoDecode == ACPI_DECODE_16);
      out->alignment = io->Alignment;
      out->address_length = io->AddressLength;
      out->minimum = io->Minimum;
      out->maximum = io->Maximum;
      break;
    }
    case ACPI_RESOURCE_TYPE_FIXED_IO: {
      ACPI_RESOURCE_FIXED_IO* io = &res->Data.FixedIo;
      out->decodes_full_space = false;
      out->alignment = 1;
      out->address_length = io->AddressLength;
      out->minimum = io->Address;
      out->maximum = io->Address;
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t resource_parse_irq(ACPI_RESOURCE* res, resource_irq_t* out) {
  switch (res->Type) {
    case ACPI_RESOURCE_TYPE_IRQ: {
      ACPI_RESOURCE_IRQ* irq = &res->Data.Irq;
      out->trigger = irq->Triggering;
      out->polarity = irq->Polarity;
      out->sharable = irq->Shareable;
      out->wake_capable = irq->WakeCapable;
      out->pin_count = irq->InterruptCount;
      if (irq->InterruptCount > std::size(out->pins)) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      for (uint8_t i = 0; (i < irq->InterruptCount) && (i < std::size(out->pins)); i++) {
        out->pins[i] = irq->Interrupts[i];
      }
      break;
    }
    case ACPI_RESOURCE_TYPE_EXTENDED_IRQ: {
      ACPI_RESOURCE_EXTENDED_IRQ* irq = &res->Data.ExtendedIrq;
      out->trigger = irq->Triggering;
      out->polarity = irq->Polarity;
      out->sharable = irq->Shareable;
      out->wake_capable = irq->WakeCapable;
      out->pin_count = irq->InterruptCount;
      if (irq->InterruptCount > std::size(out->pins)) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      for (uint8_t i = 0; (i < irq->InterruptCount) && (i < std::size(out->pins)); i++) {
        out->pins[i] = irq->Interrupts[i];
      }
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

acpi::status<fuchsia_hardware_spi_businfo::wire::SpiChannel> resource_parse_spi(
    acpi::Acpi* acpi, ACPI_HANDLE device, ACPI_RESOURCE* res, fidl::AnyArena& allocator,
    ACPI_HANDLE* resource_source) {
  auto& spi_bus = res->Data.SpiSerialBus;
  fuchsia_hardware_spi_businfo::wire::SpiChannel result(allocator);

  // Figure out which bus the SPI device belongs to.
  auto found_result = acpi->GetHandle(device, spi_bus.ResourceSource.StringPtr);
  if (!found_result.is_ok()) {
    return found_result.take_error();
  }
  *resource_source = found_result.value();
  result.set_cs(spi_bus.DeviceSelection);
  result.set_cs_polarity_high(spi_bus.DevicePolarity == ACPI_SPI_ACTIVE_HIGH);
  result.set_word_length_bits(spi_bus.DataBitLength);
  result.set_is_bus_controller(spi_bus.SlaveMode == ACPI_CONTROLLER_INITIATED);
  result.set_clock_polarity_high(spi_bus.ClockPolarity == ACPI_SPI_START_HIGH);
  result.set_clock_phase(
      spi_bus.ClockPhase == ACPI_SPI_FIRST_PHASE
          ? fuchsia_hardware_spi_businfo::wire::SpiClockPhase::kClockPhaseFirst
          : fuchsia_hardware_spi_businfo::wire::SpiClockPhase::kClockPhaseSecond);

  return zx::ok(result);
}

acpi::status<fuchsia_hardware_i2c_businfo::wire::I2CChannel> resource_parse_i2c(
    acpi::Acpi* acpi, ACPI_HANDLE device, ACPI_RESOURCE* res, fidl::AnyArena& allocator,
    ACPI_HANDLE* resource_source) {
  auto& i2c_bus = res->Data.I2cSerialBus;
  fuchsia_hardware_i2c_businfo::wire::I2CChannel result(allocator);

  // Figure out which bus the I2C device belongs to.
  auto found_result = acpi->GetHandle(device, i2c_bus.ResourceSource.StringPtr);
  if (!found_result.is_ok()) {
    return found_result.take_error();
  }
  *resource_source = found_result.value();
  result.set_address(i2c_bus.SlaveAddress);
  result.set_is_bus_controller(i2c_bus.SlaveMode == ACPI_CONTROLLER_INITIATED);
  result.set_bus_speed(i2c_bus.ConnectionSpeed);
  result.set_is_ten_bit(i2c_bus.AccessMode == ACPI_I2C_10BIT_MODE);

  return zx::ok(result);
}
