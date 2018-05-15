// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H_
#define REGISTERS_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

template <class RegType>
class TypedRegisterAddr;

// Typed registers can be used only with a specific type of RegisterIo.
template <class MmioType, class DerivedType, class IntType,
          class PrinterState = void>
class TypedRegisterBase
    : public hwreg::RegisterBase<DerivedType, IntType, PrinterState> {
 public:
  using SelfType = DerivedType;
  using ValueType = IntType;
  using Mmio = MmioType;
  using AddrType = TypedRegisterAddr<SelfType>;
  SelfType& ReadFrom(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::ReadFrom(
        static_cast<hwreg::RegisterIo*>(reg_io));
  }
  SelfType& WriteTo(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::WriteTo(
        static_cast<hwreg::RegisterIo*>(reg_io));
  }
};

template <class RegType>
class TypedRegisterAddr : public hwreg::RegisterAddr<RegType> {
 public:
  TypedRegisterAddr(uint32_t reg_addr)
      : hwreg::RegisterAddr<RegType>(reg_addr) {}

  RegType ReadFrom(typename RegType::Mmio* reg_io) {
    RegType reg;
    reg.set_reg_addr(hwreg::RegisterAddr<RegType>::addr());
    reg.ReadFrom(reg_io);
    return reg;
  }
};

// Cbus does a lot of things, but mainly seems to handle audio and video
// processing.
class CbusRegisterIo : public hwreg::RegisterIo {
 public:
  CbusRegisterIo(volatile void* mmio) : hwreg::RegisterIo(mmio) {}
};

// The DOS bus mainly seems to handle video decoding.
class DosRegisterIo : public hwreg::RegisterIo {
 public:
  DosRegisterIo(volatile void* mmio) : hwreg::RegisterIo(mmio) {}
};

// Aobus communicates with the always-on power management processor.
class AoRegisterIo : public hwreg::RegisterIo {
 public:
  AoRegisterIo(volatile void* mmio) : hwreg::RegisterIo(mmio) {}
};

// Hiubus mainly seems to handle clock control and gating.
class HiuRegisterIo : public hwreg::RegisterIo {
 public:
  HiuRegisterIo(volatile void* mmio) : hwreg::RegisterIo(mmio) {}
};

// The DMC is the DDR memory controller.
class DmcRegisterIo : public hwreg::RegisterIo {
 public:
  DmcRegisterIo(volatile void* mmio) : hwreg::RegisterIo(mmio) {}
};

#endif  // REGISTERS_H_
