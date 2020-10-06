// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include <magma_util/register_bitfields.h>
#include <magma_util/register_io.h>

namespace registers {

class A6xxRbbmSecvidTsbControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmSecvidTsbControl>(0x0000f803 << 2).FromValue(value);
  }
};

class A6xxRbbmSecvidTsbTrustedBase : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxRbbmSecvidTsbTrustedBase>(0x0000f800 << 2).FromValue(value);
  }
};

class A6xxRbbmSecvidTsbTrustedSize : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmSecvidTsbTrustedSize>(0x0000f802 << 2).FromValue(value);
  }
};

class A6xxRbbmSecvidTrustControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmSecvidTrustControl>(0x0000f400 << 2).FromValue(value);
  }
};

class A6xxRbbmClockControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(magma::RegisterIo* reg_io) {
    return magma::RegisterAddr<A6xxRbbmClockControl>(0x000000ae << 2).ReadFrom(reg_io);
  }
};

class A6xxRbbmStatus : public magma::RegisterBase {
 public:
  static constexpr uint32_t kControlProcessorAhbBusyCxMaster = 0x1;
  static constexpr uint32_t kControlProcessorAhbBusyCpMaster = 0x2;
  static constexpr uint32_t kControlProcessorBusy = 0x4;
  // Many other bits...

  bool gpu_idle() {
    // Idle if no bits are set other than 0x1
    return (reg_value() & ~kControlProcessorAhbBusyCxMaster) == 0;
  }

  static auto CreateFrom(magma::RegisterIo* reg_io) {
    return magma::RegisterAddr<A6xxRbbmStatus>(0x00000210 << 2).ReadFrom(reg_io);
  }
};

class A6xxRbbmStatusInt0 : public magma::RegisterBase {
 public:
  static auto CreateFrom(magma::RegisterIo* reg_io) {
    return magma::RegisterAddr<A6xxRbbmStatusInt0>(0x00000201 << 2).ReadFrom(reg_io);
  }
};

class A6xxVbifGateOffWrreqEnable : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxVbifGateOffWrreqEnable>(0x0000302a << 2).FromValue(value);
  }
};

class A6xxRbbmVbifClientQosControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmVbifClientQosControl>(0x00000010 << 2).FromValue(value);
  }
};

class A6xxRbbmUcheWriteRangeMax : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxRbbmUcheWriteRangeMax>(0x00000e05 << 2).FromValue(value);
  }
};

class A6xxUcheTrapBase : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxUcheTrapBase>(0x00000e09 << 2).FromValue(value);
  }
};

class A6xxUcheWriteThroughBase : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxUcheWriteThroughBase>(0x00000e07 << 2).FromValue(value);
  }
};

class A6xxUcheGmemRangeMin : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxUcheGmemRangeMin>(0x00000e0b << 2).FromValue(value);
  }
};

class A6xxUcheGmemRangeMax : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxUcheGmemRangeMax>(0x00000e0d << 2).FromValue(value);
  }
};

class A6xxUcheFilterControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxUcheFilterControl>(0x00000e18 << 2).FromValue(value);
  }
};

class A6xxUcheCacheWays : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxUcheCacheWays>(0x00000e17 << 2).FromValue(value);
  }
};

class A6xxCpRoqThresholds1 : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpRoqThresholds1>(0x000008c1 << 2).FromValue(value);
  }
};

class A6xxCpRoqThresholds2 : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpRoqThresholds2>(0x000008c2 << 2).FromValue(value);
  }
};

class A6xxCpMemPoolSize : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpMemPoolSize>(0x000008c3 << 2).FromValue(value);
  }
};

class A6xxPcDbgEcoControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxPcDbgEcoControl>(0x00009e00 << 2).FromValue(value);
  }
};

class A6xxCpAhbControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpAhbControl>(0x0000098d << 2).FromValue(value);
  }
};

class A6xxRbbmPerfCounterControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmPerfCounterControl>(0x00000500 << 2).FromValue(value);
  }
};

class A6xxCpPerfCounterCpSel0 : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpPerfCounterCpSel0>(0x000008d0 << 2).FromValue(value);
  }
};

class A6xxRbNcModeControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbNcModeControl>(0x00008e08 << 2).FromValue(value);
  }
};

class A6xxTpl1NcModeControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxTpl1NcModeControl>(0x0000b604 << 2).FromValue(value);
  }
};

class A6xxSpNcModeControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxSpNcModeControl>(0x0000ae02 << 2).FromValue(value);
  }
};

class A6xxUcheModeControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxUcheModeControl>(0x00000e01 << 2).FromValue(value);
  }
};

class A6xxRbbmInterfaceHangInterruptControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxRbbmInterfaceHangInterruptControl>(0x0000001f << 2)
        .FromValue(value);
  }
};

class A6xxUcheClientPf : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxUcheClientPf>(0x00000e19 << 2).FromValue(value);
  }
};

class A6xxCpProtectControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpProtectControl>(0x0000084f << 2).FromValue(value);
  }
};

class A6xxCpProtectBase : public magma::RegisterBase {
 public:
  static uint32_t protect(uint32_t reg, uint32_t count) {
    return protect_allow_read(reg, count) | (1 << 31);
  }

  static uint32_t protect_allow_read(uint32_t reg, uint32_t count) {
    return ((count & 0x3FFF) << 18) | (reg & 0x3FFFF);
  }
};

template <unsigned int index>
class A6xxCpProtect : public A6xxCpProtectBase {
 public:
  static_assert(index <= 25);

  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpProtect<index>>((0x850 + index) << 2).FromValue(value);
  }
};

class A6xxCpSqeInstructionBase : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxCpSqeInstructionBase>(0x00000830 << 2).FromValue(value);
  }
};

class A6xxCpSqeControl : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpSqeControl>(0x00000808 << 2).FromValue(value);
  }
};

class A6xxCpRingbufferControl : public magma::RegisterBase {
 public:
  static constexpr uint64_t kBufferSizeShift = 0;
  static constexpr uint64_t kBlockSizeShift = 8;

  void set(uint64_t ringbuffer_size, uint64_t ringbuffer_block_size) {
    uint64_t size_pow2, block_size_pow2;
    bool success = magma::get_pow2(ringbuffer_size / 8, &size_pow2);
    DASSERT(success);
    success = magma::get_pow2(ringbuffer_block_size / 8, &block_size_pow2);
    DASSERT(success);
    uint32_t value =
        magma::to_uint32((size_pow2 << kBufferSizeShift) | (block_size_pow2 << kBlockSizeShift));
    set_reg_value(value);
  }

  void disable_read_ptr_update() { set_reg_value(reg_value() | 0x08000000); }

  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpRingbufferControl>(0x00000802 << 2).FromValue(value);
  }
};

class A6xxCpRingbufferBase : public magma::RegisterPairBase {
 public:
  static auto CreateFrom(uint64_t value) {
    return magma::RegisterAddr<A6xxCpRingbufferBase>(0x00000800 << 2).FromValue(value);
  }
};

class A6xxCpRingbufferReadPointer : public magma::RegisterBase {
 public:
  static auto CreateFrom(magma::RegisterIo* reg_io) {
    return magma::RegisterAddr<A6xxCpRingbufferReadPointer>(0x00000806 << 2).ReadFrom(reg_io);
  }
};

class A6xxCpRingbufferWritePointer : public magma::RegisterBase {
 public:
  static auto CreateFrom(uint32_t value) {
    return magma::RegisterAddr<A6xxCpRingbufferWritePointer>(0x00000807 << 2).FromValue(value);
  }
};

}  // namespace registers

#endif  // REGISTERS_H
