// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/io_port.h"

#include <time.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/bin/vmm/bits.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/rtc.h"

// clang-format off

// PIC constants.
constexpr uint16_t kPicDataPort                 = 1;
constexpr uint8_t kPicInvalid                   = UINT8_MAX;

// PM1 relative port mappings.
constexpr uint16_t kPm1StatusPortOffset         = 0;
constexpr uint16_t kPm1EnablePortOffset         = 2;
constexpr uint16_t kPm1ControlPortOffset        = kPm1ControlPort - kPm1EventPort;
constexpr uint16_t kPm1Size                     = kPm1EnablePortOffset + 1;

// CMOS relative port mappings.
constexpr uint16_t kCmosIndexPort               = 0;
constexpr uint16_t kCmosDataPort                = 1;

// CMOS register addresses.
constexpr uint8_t kCmosRegisterRtcSeconds       = 0;
constexpr uint8_t kCmosRegisterRtcSecondsAlarm  = 1;
constexpr uint8_t kCmosRegisterRtcMinutes       = 2;
constexpr uint8_t kCmosRegisterRtcMinutesAlarm  = 3;
constexpr uint8_t kCmosRegisterRtcHours         = 4;
constexpr uint8_t kCmosRegisterRtcHoursAlarm    = 5;
constexpr uint8_t kCmosRegisterRtcDayOfMonth    = 7;
constexpr uint8_t kCmosRegisterRtcMonth         = 8;
constexpr uint8_t kCmosRegisterRtcYear          = 9;
constexpr uint8_t kCmosRegisterA                = 10;
constexpr uint8_t kCmosRegisterB                = 11;
constexpr uint8_t kCmosRegisterC                = 12;
constexpr uint8_t kCmosRegisterShutdownStatus   = 15;

// CMOS register B flags.
constexpr uint8_t kCmosRegisterBDaylightSavings = 1 << 0;
constexpr uint8_t kCmosRegisterBHourFormat      = 1 << 1;
constexpr uint8_t kCmosRegisterBInterruptMask   = 0x70;

// I8042 relative port mappings.
constexpr uint16_t kI8042DataPort               = 0x0;
constexpr uint16_t kI8042CommandPort            = 0x4;

// I8042 status flags.
constexpr uint8_t kI8042StatusOutputFull        = 1 << 0;

// I8042 test constants.
constexpr uint8_t kI8042CommandTest             = 0xaa;
constexpr uint8_t kI8042DataTestResponse        = 0x55;

// I8237 DMA Controller relative port mappings.
// See Intel Series 7 Platform Host Controller Hub, Table 13-2.
constexpr uint16_t kI8237DmaPage0               = 0x7;

// CMOS ports.
static constexpr uint64_t kCmosBase             = 0x70;
static constexpr uint64_t kCmosSize             = 0x2;

// I8042 ports.
static constexpr uint64_t kI8042Base            = 0x60;

// I8237 DMA Controller ports.
// See Intel Series 7 Platform Host Controller Hub, Table 13-2.
static constexpr uint64_t kI8237Base            = 0x80;

// Power states as defined in the DSDT.
//
// We only implement a transition from S0 to S5 to trigger guest termination.
static constexpr uint64_t kSlpTyp5              = 0x1;

// PIC ports.
static constexpr uint64_t kPic1Base             = 0x20;
static constexpr uint64_t kPic2Base             = 0xa0;
static constexpr uint64_t kPicSize              = 0x2;

// PIT ports.
static constexpr uint64_t kPitBase              = 0x40;
static constexpr uint64_t kPitSize              = 0x4;

// See Intel Series 7 Platform Host Controller Hub, Section 5.4.1.9:
// If the [IO port] is not claimed by any peripheral (and subsequently aborted),
// the PCH returns a value of all 1s (FFh) to the processor.
constexpr uint8_t kPortRemoved                  = 0xff;

// clang-format on

zx_status_t PicHandler::Init(Guest* guest, uint16_t base) {
  return guest->CreateMapping(TrapType::PIO_SYNC, base, kPicSize, 0, this);
}

zx_status_t PicHandler::Read(uint64_t addr, IoValue* value) const {
  if (addr == kPicDataPort) {
    value->access_size = 1;
    value->u8 = kPicInvalid;
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PicHandler::Write(uint64_t addr, const IoValue& value) { return ZX_OK; }

zx_status_t PitHandler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kPitBase, kPitSize, 0, this);
}

zx_status_t PitHandler::Read(uint64_t addr, IoValue* value) const { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t PitHandler::Write(uint64_t addr, const IoValue& value) { return ZX_OK; }

zx_status_t Pm1Handler::Init(Guest* guest) {
  // Map 2 distinct register blocks for event and control registers.
  zx_status_t status = guest->CreateMapping(TrapType::PIO_SYNC, kPm1EventPort, kPm1Size, 0, this);
  if (status != ZX_OK) {
    return status;
  }
  return guest->CreateMapping(TrapType::PIO_SYNC, kPm1ControlPort, kPm1Size, kPm1ControlPort, this);
}

zx_status_t Pm1Handler::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case kPm1StatusPortOffset:
      value->access_size = 2;
      value->u16 = 0;
      break;
    case kPm1EnablePortOffset: {
      value->access_size = 2;
      std::lock_guard<std::mutex> lock(mutex_);
      value->u16 = enable_;
      break;
    }
    case kPm1ControlPortOffset:
      value->u32 = 0;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Pm1Handler::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case kPm1StatusPortOffset:
      break;
    case kPm1EnablePortOffset: {
      if (value.access_size != 2) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      enable_ = value.u16;
      break;
    }
    case kPm1ControlPortOffset: {
      uint16_t slp_en = bit_shift(value.u16, 13);
      uint16_t slp_type = bits_shift(value.u16, 12, 10);
      if (slp_en != 0) {
        // Only power-off transitions are supported.
        return slp_type == kSlpTyp5 ? ZX_ERR_STOP : ZX_ERR_NOT_SUPPORTED;
      }
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

static uint8_t to_bcd(int binary) {
  return static_cast<uint8_t>(((binary / 10) << 4) | (binary % 10));
}

zx_status_t CmosHandler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kCmosBase, kCmosSize, 0, this);
}

zx_status_t CmosHandler::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case kCmosDataPort: {
      value->access_size = 1;
      uint8_t cmos_index;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cmos_index = index_;
      }
      return ReadCmosRegister(cmos_index, &value->u8);
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t CmosHandler::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case kCmosDataPort: {
      uint8_t cmos_index;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cmos_index = index_;
      }
      return WriteCmosRegister(cmos_index, value.u8);
    }
    case kCmosIndexPort: {
      if (value.access_size != 1) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      index_ = value.u8;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t CmosHandler::ReadCmosRegister(uint8_t cmos_index, uint8_t* value) const {
  time_t now = rtc_time();
  struct tm tm;
  if (localtime_r(&now, &tm) == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  switch (cmos_index) {
    case kCmosRegisterRtcSeconds:
      *value = to_bcd(tm.tm_sec);
      break;
    case kCmosRegisterRtcMinutes:
      *value = to_bcd(tm.tm_min);
      break;
    case kCmosRegisterRtcHours:
      *value = to_bcd(tm.tm_hour);
      break;
    case kCmosRegisterRtcDayOfMonth:
      *value = to_bcd(tm.tm_mday);
      break;
    case kCmosRegisterRtcMonth:
      // struct tm represents months as 0-11, RTC uses 1-12.
      *value = to_bcd(tm.tm_mon + 1);
      break;
    case kCmosRegisterRtcYear: {
      // RTC expects the number of years since 2000.
      int year = tm.tm_year - 100;
      if (year < 0) {
        year = 0;
      }
      *value = to_bcd(year);
      break;
    }
    case kCmosRegisterA:
      // Ensure that UIP is 0. Other values (clock frequency) are obsolete.
      *value = 0;
      break;
    case kCmosRegisterB:
      *value = kCmosRegisterBHourFormat;
      if (tm.tm_isdst) {
        *value |= kCmosRegisterBDaylightSavings;
      }
      break;
    // Alarms are not implemented but allow reads of the registers.
    case kCmosRegisterRtcSecondsAlarm:
    case kCmosRegisterRtcMinutesAlarm:
    case kCmosRegisterRtcHoursAlarm:
    case kCmosRegisterC:
      *value = 0;
      break;
    default:
      FX_LOGS(ERROR) << "Unsupported CMOS register read 0x" << std::hex
                     << static_cast<uint32_t>(cmos_index);
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t CmosHandler::WriteCmosRegister(uint8_t cmos_index, uint8_t value) {
  switch (cmos_index) {
    case kCmosRegisterA:
      return ZX_OK;
    case kCmosRegisterB:
      // No interrupts are implemented.
      if (value & kCmosRegisterBInterruptMask) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      return ZX_OK;
    case kCmosRegisterRtcSeconds:
    case kCmosRegisterRtcMinutes:
    case kCmosRegisterRtcHours:
    case kCmosRegisterRtcDayOfMonth:
    case kCmosRegisterRtcMonth:
    case kCmosRegisterRtcYear:
    case kCmosRegisterShutdownStatus:
      // Ignore attempts to write to the RTC or shutdown status register.
      return ZX_OK;
    default:
      FX_LOGS(ERROR) << "Unsupported CMOS register write 0x" << std::hex
                     << static_cast<uint32_t>(cmos_index);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t I8042Handler::Init(Guest* guest) {
  zx_status_t status = guest->CreateMapping(TrapType::PIO_SYNC, kI8042Base + kI8042DataPort, 1,
                                            kI8042DataPort, this);
  if (status != ZX_OK) {
    return status;
  }

  return guest->CreateMapping(TrapType::PIO_SYNC, kI8042Base + kI8042CommandPort, 1,
                              kI8042CommandPort, this);
}

zx_status_t I8042Handler::Read(uint64_t port, IoValue* value) const {
  switch (port) {
    case kI8042DataPort: {
      value->access_size = 1;
      std::lock_guard<std::mutex> lock(mutex_);
      value->u8 = command_ == kI8042CommandTest ? kI8042DataTestResponse : 0;
      break;
    }
    case kI8042CommandPort:
      value->access_size = 1;
      value->u8 = kI8042StatusOutputFull;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t I8042Handler::Write(uint64_t port, const IoValue& value) {
  switch (port) {
    case kI8042DataPort:
    case kI8042CommandPort: {
      if (value.access_size != 1) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      command_ = value.u8;
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t I8237Handler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kI8237Base + kI8237DmaPage0, 1, kI8237DmaPage0,
                              this);
}

zx_status_t I8237Handler::Read(uint64_t port, IoValue* value) const {
  if (port != kI8237DmaPage0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  value->access_size = 1;
  value->u8 = kPortRemoved;
  return ZX_OK;
}

zx_status_t I8237Handler::Write(uint64_t addr, const IoValue& value) {
  return ZX_ERR_NOT_SUPPORTED;
}

// Processor Interface Registers
//
// See Intel Series 7 Platform Host Controller Hub, Section 13.7:
// Processor Interface Registers
constexpr uint8_t kNmiStatusControlPort = 0x61;
constexpr uint8_t kNmiStatusControlOffset = 0;

zx_status_t ProcessorInterfaceHandler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kNmiStatusControlPort, 1, kNmiStatusControlOffset,
                              this);
}

zx_status_t ProcessorInterfaceHandler::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case kNmiStatusControlOffset:
      value->u8 = nmi_sc_;
      return ZX_OK;
    default:
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t ProcessorInterfaceHandler::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case kNmiStatusControlOffset:
      // The upper 4 bits are all read-only to the guest.
      nmi_sc_ |= value.u8 & bit_mask<uint8_t>(4);
      return ZX_OK;
    default:
      return ZX_ERR_INTERNAL;
  }
}

zx_status_t IoPort::Init(Guest* guest) {
  zx_status_t status;
  status = pic1_.Init(guest, kPic1Base);
  if (status != ZX_OK) {
    return status;
  }
  status = pic2_.Init(guest, kPic2Base);
  if (status != ZX_OK) {
    return status;
  }
  status = pit_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }
  status = pm1_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }
  status = cmos_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }
  status = i8042_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }
  status = i8237_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }
  status = proc_iface_.Init(guest);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}
