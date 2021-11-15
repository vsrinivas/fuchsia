// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_PORT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_PORT_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

#include "src/virtualization/bin/vmm/arch/x64/rtc_mc146818.h"
#include "src/virtualization/bin/vmm/io.h"

// PM1 ports. Exposed here for ACPI.
static constexpr uint64_t kPm1EventPort = 0x1000;
static constexpr uint64_t kPm1ControlPort = 0x2000;

// CMOS relative port mappings.
constexpr uint16_t kCmosIndexPort = 0;
constexpr uint16_t kCmosDataPort = 1;

// CMOS reboot reason byte address.
//
// Zircon uses this CMOS register to indicate the reason for its last reboot
// (e.g., a graceful reboot, panic, OTA, etc). We don't attempt to persist
// this register across VM runs, but do emulate basic reads/writes to it to
// avoid Zircon crashing during system shutdown.
constexpr uint8_t kCmosRebootReason = 0x30;

class Guest;

class PicHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, uint16_t base);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "PIC"; }
};

class PitHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "PIT"; }
};

class Pm1Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "PM1"; }

 private:
  mutable std::mutex mutex_;
  uint16_t enable_ __TA_GUARDED(mutex_) = 0;
};

class CmosHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "CMOS"; }

 private:
  zx_status_t ReadCmosRegister(uint8_t cmos_index, uint8_t* value);
  zx_status_t WriteCmosRegister(uint8_t cmos_index, uint8_t value);
  mutable std::mutex mutex_;
  uint8_t index_ __TA_GUARDED(mutex_) = 0;
  uint8_t reboot_reason_byte_ __TA_GUARDED(mutex_) = 0;

  RtcMc146818 rtc_;
};

class I8042Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "I8042"; }

 private:
  mutable std::mutex mutex_;
  uint8_t command_ __TA_GUARDED(mutex_) = 0;
};

class I8237Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "I8237"; }
};

class ProcessorInterfaceHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "Processor Interface"; }

 private:
  uint8_t nmi_sc_ = 0;
};

class IoPort {
 public:
  zx_status_t Init(Guest* guest);

 private:
  PicHandler pic1_;
  PicHandler pic2_;
  PitHandler pit_;
  Pm1Handler pm1_;
  CmosHandler cmos_;
  I8042Handler i8042_;
  I8237Handler i8237_;
  ProcessorInterfaceHandler proc_iface_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_PORT_H_
