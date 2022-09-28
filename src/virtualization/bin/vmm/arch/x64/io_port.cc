// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/io_port.h"

#include <lib/syslog/cpp/macros.h>
#include <time.h>

#include "src/virtualization/bin/vmm/bits.h"
#include "src/virtualization/bin/vmm/guest.h"

// clang-format off

// PIC constants.
constexpr uint16_t kPicDataPort                 = 1;
constexpr uint8_t kPicInvalid                   = UINT8_MAX;

// PM1 relative port mappings.
constexpr uint16_t kPm1StatusPortOffset         = 0;
constexpr uint16_t kPm1EnablePortOffset         = 2;
constexpr uint16_t kPm1ControlPortOffset        = kPm1ControlPort - kPm1EventPort;
constexpr uint16_t kPm1Size                     = kPm1EnablePortOffset + 1;

// CMOS register addresses.
constexpr uint8_t kCmosRegisterShutdownStatus   = 15;

// I8042 relative port mappings.
constexpr uint16_t kI8042DataPort               = 0x0;
constexpr uint16_t kI8042CommandPort            = 0x4;

// I8042 status flags.
constexpr uint8_t kI8042StatusOutputFull        = 1 << 0;

// I8042 commands.
constexpr uint8_t kI8042PulseResetLow           = 0xfe;

// I8042 test constants.
constexpr uint8_t kI8042CommandTest             = 0xaa;
constexpr uint8_t kI8042DataTestResponse        = 0x55;

// I8237 DMA Controller relative port mappings.
// See Intel Series 7 Platform Host Controller Hub, Table 13-2.
constexpr uint16_t kI8237DmaPage0               = 0x7;

// CMOS ports.
static constexpr uint64_t kCmosBase             = 0x70;
static constexpr uint64_t kCmosSize             = 0x2;

// CMOS constants.
constexpr uint64_t kCmosNmiDisabled             = 0x80;

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

zx_status_t PicHandler::Read(uint64_t addr, IoValue* value) {
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

zx_status_t PitHandler::Read(uint64_t addr, IoValue* value) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t PitHandler::Write(uint64_t addr, const IoValue& value) { return ZX_OK; }

zx_status_t Pm1Handler::Init(Guest* guest) {
  // Map 2 distinct register blocks for event and control registers.
  zx_status_t status = guest->CreateMapping(TrapType::PIO_SYNC, kPm1EventPort, kPm1Size, 0, this);
  if (status != ZX_OK) {
    return status;
  }
  return guest->CreateMapping(TrapType::PIO_SYNC, kPm1ControlPort, kPm1Size, kPm1ControlPortOffset,
                              this);
}

zx_status_t Pm1Handler::Read(uint64_t addr, IoValue* value) {
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
        return ZX_ERR_IO;
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
        if (slp_type != kSlpTyp5) {
          FX_LOGS(ERROR) << "Unsupported sleep state transition. Guest requested sleep type "
                         << slp_type;
          return ZX_ERR_NOT_SUPPORTED;
        }

        // Power off.
        //
        // Returning ZX_ERR_CANCELED will cause the VMM to gracefully shut down.
        return ZX_ERR_CANCELED;
      }
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t CmosHandler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kCmosBase, kCmosSize, 0, this);
}

zx_status_t CmosHandler::Read(uint64_t addr, IoValue* value) {
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
        return ZX_ERR_IO;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      // The kCmosNmiDisabled bit may be set which essentially means that there is a CMOS update
      // in progress. This bit must be ignored when determining the CMOS index.
      index_ = value.u8 & ~kCmosNmiDisabled;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t CmosHandler::ReadCmosRegister(uint8_t cmos_index, uint8_t* value) {
  // Currently the RTC is the only implemented CMOS registers
  if (RtcMc146818::IsValidRegister(cmos_index)) {
    return rtc_.ReadRegister(static_cast<RtcMc146818::Register>(cmos_index), value);
  }
  if (cmos_index == kCmosRebootReason) {
    std::lock_guard<std::mutex> lock(mutex_);
    *value = reboot_reason_byte_;
    return ZX_OK;
  }

  FX_LOGS(ERROR) << "Unsupported CMOS register read 0x" << std::hex
                 << static_cast<uint32_t>(cmos_index);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t CmosHandler::WriteCmosRegister(uint8_t cmos_index, uint8_t value) {
  if (RtcMc146818::IsValidRegister(cmos_index))
    return rtc_.WriteRegister(static_cast<RtcMc146818::Register>(cmos_index), value);
  if (cmos_index == kCmosRegisterShutdownStatus) {
    // Ignore attempts to write to shutdown status register.
    return ZX_OK;
  }
  if (cmos_index == kCmosRebootReason) {
    std::lock_guard<std::mutex> lock(mutex_);
    reboot_reason_byte_ = value;
    return ZX_OK;
  }

  FX_LOGS(ERROR) << "Unsupported CMOS register write 0x" << std::hex
                 << static_cast<uint32_t>(cmos_index);
  return ZX_ERR_NOT_SUPPORTED;
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

zx_status_t I8042Handler::Read(uint64_t port, IoValue* value) {
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
      return ZX_OK;
    case kI8042CommandPort: {
      if (value.access_size != 1) {
        return ZX_ERR_IO;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      command_ = value.u8;
      if (command_ == kI8042PulseResetLow) {
        // Writing 0xfe to the command port triggers a restart, regardless of what state the
        // CPU is in. Since we don't support restarting guests, writing this value is equivalent
        // to an unconditional and immediate shutdown.
        return ZX_ERR_CANCELED;
      }
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t I8237Handler::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::PIO_SYNC, kI8237Base + kI8237DmaPage0, 1, kI8237DmaPage0,
                              this);
}

zx_status_t I8237Handler::Read(uint64_t port, IoValue* value) {
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

zx_status_t ProcessorInterfaceHandler::Read(uint64_t addr, IoValue* value) {
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
