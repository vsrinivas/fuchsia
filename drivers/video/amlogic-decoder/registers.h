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

#define DEFINE_REGISTER(name, type, address)                           \
  class name : public TypedRegisterBase<type, name, uint32_t> {        \
   public:                                                             \
    static auto Get() { return TypedRegisterAddr<name>(address * 4); } \
  };

#define REGISTER_NAME(name, type, address)                      \
  class name : public TypedRegisterBase<type, name, uint32_t> { \
   public:                                                      \
    static auto Get() { return AddrType(address * 4); }

// clang-format off
DEFINE_REGISTER(Mpsr, DosRegisterIo, 0x301);
DEFINE_REGISTER(Cpsr, DosRegisterIo, 0x321);
DEFINE_REGISTER(ImemDmaCtrl, DosRegisterIo, 0x340);
DEFINE_REGISTER(ImemDmaAdr, DosRegisterIo, 0x341);
DEFINE_REGISTER(ImemDmaCount, DosRegisterIo, 0x342);
DEFINE_REGISTER(DosSwReset0, DosRegisterIo, 0x3f00);
DEFINE_REGISTER(DosGclkEn, DosRegisterIo, 0x3f01);
DEFINE_REGISTER(DosMemPdVdec, DosRegisterIo, 0x3f30);
DEFINE_REGISTER(DosVdecMcrccStallCtrl, DosRegisterIo, 0x3f40);

DEFINE_REGISTER(AoRtiGenPwrSleep0, AoRegisterIo, 0x3a);
DEFINE_REGISTER(AoRtiGenPwrIso0, AoRegisterIo, 0x3b);

REGISTER_NAME(HhiGclkMpeg0, HiuRegisterIo, 0x50)
  DEF_BIT(1, dos);
};

REGISTER_NAME(HhiGclkMpeg1, HiuRegisterIo, 0x51)
  DEF_BIT(25, u_parser_top);
  DEF_FIELD(13, 6, aiu);
  DEF_BIT(4, demux);
  DEF_BIT(2, audio_in);
};

REGISTER_NAME(HhiGclkMpeg2, HiuRegisterIo, 0x52)
  DEF_BIT(25, vpu_interrupt);
};

REGISTER_NAME(HhiVdecClkCntl, HiuRegisterIo, 0x78)
  DEF_BIT(8, vdec_en);
  DEF_FIELD(11, 9, vdec_sel);
  DEF_FIELD(6, 0, vdec_div);
};

REGISTER_NAME(DmcReqCtrl, DmcRegisterIo, 0x0)
  DEF_BIT(13, vdec);
};

// clang-format on

#undef REGISTER_NAME
#undef DEFINE_REGISTER

#endif  // REGISTERS_H_
