// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_

#include <lib/mmio/mmio.h>

#include <ddk/mmio-buffer.h>
#include <hwreg/bitfields.h>

template <class RegType>
class TypedRegisterAddr;

// Typed registers can be used only with a specific type of RegisterIo.
template <class MmioType, class DerivedType, class IntType, class PrinterState = void>
class TypedRegisterBase : public hwreg::RegisterBase<DerivedType, IntType, PrinterState> {
 public:
  using SelfType = DerivedType;
  using ValueType = IntType;
  using Mmio = MmioType;
  using AddrType = TypedRegisterAddr<SelfType>;
  SelfType& ReadFrom(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::ReadFrom(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
  SelfType& WriteTo(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::WriteTo(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
};

template <class RegType>
class TypedRegisterAddr : public hwreg::RegisterAddr<RegType> {
 public:
  TypedRegisterAddr(uint32_t reg_addr) : hwreg::RegisterAddr<RegType>(reg_addr) {}

  RegType ReadFrom(typename RegType::Mmio* reg_io) {
    RegType reg;
    reg.set_reg_addr(hwreg::RegisterAddr<RegType>::addr());
    reg.ReadFrom(reg_io);
    return reg;
  }
};

// Cbus does a lot of things, but mainly seems to handle audio and video
// processing.
class CbusRegisterIo : public ddk::MmioBuffer {
 public:
  CbusRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// The DOS bus mainly seems to handle video decoding.
class DosRegisterIo : public ddk::MmioBuffer {
 public:
  DosRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// Aobus communicates with the always-on power management processor.
class AoRegisterIo : public ddk::MmioBuffer {
 public:
  AoRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// Hiubus mainly seems to handle clock control and gating.
class HiuRegisterIo : public ddk::MmioBuffer {
 public:
  HiuRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

class ResetRegisterIo : public ddk::MmioView {
 public:
  ResetRegisterIo(const mmio_buffer_t& mmio, zx_off_t off) : ddk::MmioView(mmio, off) {}
};

#define DEFINE_REGISTER(name, type, address)                           \
  class name : public TypedRegisterBase<type, name, uint32_t> {        \
   public:                                                             \
    static auto Get() { return TypedRegisterAddr<name>((address)*4); } \
  };

#define REGISTER_NAME(name, type, address)                      \
  class name : public TypedRegisterBase<type, name, uint32_t> { \
   public:                                                      \
    static auto Get() { return AddrType((address)*4); }

// TODO define registers

#undef REGISTER_NAME
#undef DEFINE_REGISTER

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_
