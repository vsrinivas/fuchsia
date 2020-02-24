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

// The DOS bus mainly seems to handle video decoding/encoding.
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

#define DEFINE_REGISTER(name, type, address)                           \
  class name : public TypedRegisterBase<type, name, uint32_t> {        \
   public:                                                             \
    static auto Get() { return TypedRegisterAddr<name>((address)*4); } \
  };

#define REGISTER_BEGIN(name, type, address)                     \
  class name : public TypedRegisterBase<type, name, uint32_t> { \
   public:                                                      \
    static auto Get() { return AddrType((address)*4); }

#define REGISTER_END \
  }                  \
  ;

REGISTER_BEGIN(AoRtiGenPwrSleep0, AoRegisterIo, 0x3a)
DEF_BIT(1, dos_hcodec_d1_pwr_off);
DEF_BIT(0, dos_hcodec_pwr_off);
REGISTER_END

REGISTER_BEGIN(AoRtiGenPwrIso0, AoRegisterIo, 0x3b)
DEF_BIT(5, dos_hcodec_iso_out_en);
DEF_BIT(4, dos_hcodec_iso_in_en);
REGISTER_END

REGISTER_BEGIN(DosSwReset1, DosRegisterIo, 0x3f07)
enum { kAll = 0xffffffff, kNone = 0 };
DEF_BIT(17, hcodec_qdct);
DEF_BIT(16, hcodec_vlc);
DEF_BIT(14, hcodec_afifo);
DEF_BIT(13, hcodec_ddr);
DEF_BIT(12, hcodec_ccpu);
DEF_BIT(11, hcodec_mcpu);
DEF_BIT(10, hcodec_psc);
DEF_BIT(9, hcodec_pic_dc);
DEF_BIT(8, hcodec_dblk);
DEF_BIT(7, hcodec_mc);
DEF_BIT(6, hcodec_iqidct);
DEF_BIT(5, hcodec_vififo);
DEF_BIT(4, hcodec_vld_part);
DEF_BIT(3, hcodec_vld);
DEF_BIT(2, hcodec_assist);
REGISTER_END

REGISTER_BEGIN(DosGclkEn0, DosRegisterIo, 0x3f01)
DEF_FIELD(27, 12, hcodec_en);
REGISTER_END

REGISTER_BEGIN(DosGenCtrl0, DosRegisterIo, 0x3f02)
DEF_BIT(0, hcodec_auto_clock_gate);
REGISTER_END

REGISTER_BEGIN(DosMemPdHcodec, DosRegisterIo, 0x3f32)
REGISTER_END

REGISTER_BEGIN(HcodecAssistMmcCtrl1, DosRegisterIo, 0x1002)
enum { kCtrl = 0x32 };
REGISTER_END

DEFINE_REGISTER(HcodecAssistAmr1Int0, DosRegisterIo, 0x1025)
DEFINE_REGISTER(HcodecAssistAmr1Int1, DosRegisterIo, 0x1026)
DEFINE_REGISTER(HcodecAssistAmr1Int3, DosRegisterIo, 0x1028)

DEFINE_REGISTER(HcodecMpsr, DosRegisterIo, 0x1301)
DEFINE_REGISTER(HcodecCpsr, DosRegisterIo, 0x1321)

REGISTER_BEGIN(HcodecImemDmaCtrl, DosRegisterIo, 0x1340)
enum { kCtrl = 0x7 };
DEF_BIT(15, ready);
DEF_FIELD(18, 16, ctrl);
REGISTER_END

DEFINE_REGISTER(HcodecImemDmaAdr, DosRegisterIo, 0x1341)
DEFINE_REGISTER(HcodecImemDmaCount, DosRegisterIo, 0x1342)

DEFINE_REGISTER(HcodecIdrPicId, DosRegisterIo, 0x1ac5)
DEFINE_REGISTER(HcodecFrameNumber, DosRegisterIo, 0x1ac6)
DEFINE_REGISTER(HcodecPicOrderCntLsb, DosRegisterIo, 0x1ac7)
DEFINE_REGISTER(HcodecLog2MaxPicOrderCntLsb, DosRegisterIo, 0x1ac8)
DEFINE_REGISTER(HcodecLog2MaxFrameNum, DosRegisterIo, 0x1ac9)
DEFINE_REGISTER(HcodecAnc0BufferId, DosRegisterIo, 0x1aca)
DEFINE_REGISTER(HcodecQpPicture, DosRegisterIo, 0x1acb)

DEFINE_REGISTER(HcodecVlcTotalBytes, DosRegisterIo, 0x1d1a)
DEFINE_REGISTER(HcodecVlcConfig, DosRegisterIo, 0x1d01)
DEFINE_REGISTER(HcodecVlcIntControl, DosRegisterIo, 0x1d30)

REGISTER_BEGIN(HhiVdecClkCntl, HiuRegisterIo, 0x78)
DEF_FIELD(27, 25, hcodec_clk_sel);
DEF_BIT(24, hcodec_clk_en);
DEF_FIELD(22, 16, hcodec_clk_div);
REGISTER_END

REGISTER_BEGIN(HhiGclkMpeg0, HiuRegisterIo, 0x50)
DEF_BIT(1, dos);
REGISTER_END

#undef REGISTER_BEGIN
#undef REGISTER_END
#undef DEFINE_REGISTER

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_
