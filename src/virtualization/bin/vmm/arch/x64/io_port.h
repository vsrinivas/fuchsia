// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_PORT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_PORT_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"

// clang-format off

// PM1 ports. Exposed here for ACPI.
static constexpr uint64_t kPm1EventPort   = 0x1000;
static constexpr uint64_t kPm1ControlPort = 0x2000;

// clang-format on

class Guest;

class PicHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, uint16_t base);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class PitHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class Pm1Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  mutable std::mutex mutex_;
  uint16_t enable_ __TA_GUARDED(mutex_) = 0;
};

class CmosHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  zx_status_t ReadCmosRegister(uint8_t cmos_index, uint8_t* value) const;
  zx_status_t WriteCmosRegister(uint8_t cmos_index, uint8_t value);
  mutable std::mutex mutex_;
  uint8_t index_ __TA_GUARDED(mutex_) = 0;
};

class I8042Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  mutable std::mutex mutex_;
  uint8_t command_ __TA_GUARDED(mutex_) = 0;
};

class I8237Handler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
};

class ProcessorInterfaceHandler : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

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
