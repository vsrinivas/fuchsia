// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include <bitset>
#include <vector>

#include "device_id.h"
#include "hwreg/bitfields.h"
#include "magma_util/macros.h"
#include "msd_intel_register_io.h"
#include "types.h"

namespace registers {

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.733
class GmchGraphicsControl {
 public:
  static constexpr uint32_t kOffset = 0x50;
  static constexpr uint32_t kGttSizeShift = 6;
  static constexpr uint32_t kGttSizeMask = 0x3;

  static uint32_t gtt_size(uint32_t val) {
    unsigned int size = (val >> kGttSizeShift) & kGttSizeMask;
    return (size == 0) ? 0 : (1 << size) * 1024 * 1024;
  }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.712
class HardwareStatusPageAddress {
 public:
  static constexpr uint32_t kOffset = 0x80;

  static void write(MsdIntelRegisterIo* reg_io, uint32_t mmio_base, uint32_t gtt_addr) {
    DASSERT(magma::is_page_aligned(gtt_addr));
    reg_io->Write32(gtt_addr, mmio_base + kOffset);
    reg_io->mmio()->PostingRead32(mmio_base + kOffset);
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.500
class PatIndex {
 public:
  static constexpr uint32_t kOffsetLow = 0x40E0;
  static constexpr uint32_t kOffsetHigh = 0x40E4;

  static constexpr uint8_t kUncacheable = 0;
  static constexpr uint8_t kWriteCombining = 1;
  static constexpr uint8_t kWriteThrough = 2;
  static constexpr uint8_t kWriteBack = 3;
  static constexpr uint8_t kMemTypeMask = 0x3;

  static constexpr uint8_t kEllc = 0;
  static constexpr uint8_t kLlc = 1;
  static constexpr uint8_t kLlcEllc = 2;
  static constexpr uint8_t kTargetCacheMask = 3;

  static constexpr uint8_t kLruAgeFromUncore = 0;
  static constexpr uint8_t kLruAgeZero = 1;
  static constexpr uint8_t kLruAgeNoChange = 2;
  static constexpr uint8_t kLruAgeThree = 3;
  static constexpr uint8_t kLruAgeMask = 0x3;

  static void write(MsdIntelRegisterIo* reg_io, uint64_t val) {
    reg_io->Write32(static_cast<uint32_t>(val), kOffsetLow);
    reg_io->Write32(static_cast<uint32_t>(val >> 32), kOffsetHigh);
  }

  static uint64_t ppat(unsigned int index, uint8_t lru_age, uint8_t target_cache,
                       uint8_t mem_type) {
    DASSERT((lru_age & ~kLruAgeMask) == 0);
    DASSERT((target_cache & ~kTargetCacheMask) == 0);
    DASSERT((mem_type & ~kMemTypeMask) == 0);
    uint64_t ppat = (lru_age << 4) | (target_cache << 2) | mem_type;
    return ppat << (index * 8);
  }
};

// PAT_INDEX
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part2-rev2_1.pdf
// p.645
class PatIndexGen12 {
 public:
  static constexpr uint32_t kOffset = 0x4800;
  static constexpr uint32_t kIndexCount = 8;

  enum MemType { UNCACHEABLE = 0, WRITE_COMBINING = 1, WRITE_THROUGH = 2, WRITE_BACK = 3 };

  static void write(MsdIntelRegisterIo* register_io, uint32_t index, MemType type) {
    DASSERT(index < kIndexCount);
    register_io->Write32(type, kOffset + index * sizeof(uint32_t));
  }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.438
// and intel-gfx-prm-osrc-bdw-vol02d-commandreference-structures_3.pdf p.107
// Note: this register exists in all hardware but ExeclistSubmitQueue is used from gen12.
class ExeclistSubmitPort {
 public:
  static constexpr uint32_t kSubmitOffset = 0x230;
  static constexpr uint32_t kStatusOffset = 0x234;

  static uint64_t context_descriptor(gpu_addr_t gpu_addr, uint32_t context_id, bool ppgtt_enable) {
    constexpr uint32_t kValid = 1;
    constexpr uint32_t kLegacyMode48bitPpgtt = 3 << 3;
    constexpr uint32_t kLegacyModePpgttEnable = 1 << 8;
    constexpr uint32_t kContextIdShift = 32;

    uint64_t desc = gpu_addr;
    desc |= kValid;
    desc |= kLegacyMode48bitPpgtt;
    if (ppgtt_enable)
      desc |= kLegacyModePpgttEnable;
    desc |= static_cast<uint64_t>(context_id) << kContextIdShift;
    return desc;
  }

  static void write(MsdIntelRegisterIo* reg_io, uint32_t mmio_base, uint64_t descriptor1,
                    uint64_t descriptor0) {
    uint32_t desc[4]{magma::upper_32_bits(descriptor1), magma::lower_32_bits(descriptor1),
                     magma::upper_32_bits(descriptor0), magma::lower_32_bits(descriptor0)};

    // The last write triggers the context load.
    reg_io->Write32(desc[0], mmio_base + kSubmitOffset);
    reg_io->Write32(desc[1], mmio_base + kSubmitOffset);
    reg_io->Write32(desc[2], mmio_base + kSubmitOffset);
    reg_io->Write32(desc[3], mmio_base + kSubmitOffset);

    reg_io->mmio()->PostingRead32(mmio_base + kStatusOffset);
  }
};

// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.896
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02d-commandreference-structures_0.pdf
// p.275
class ExeclistSubmitQueue {
 public:
  static constexpr uint32_t kOffset = 0x510;

  enum EngineType {
    RENDER = 0,
    VIDEO = 1,
  };

  static constexpr uint64_t context_descriptor(EngineType type, uint32_t instance,
                                               uint32_t context_id, gpu_addr_t gpu_addr) {
    constexpr uint32_t kValid = 1;
    constexpr uint32_t kLegacyMode48bitPpgtt = 3 << 3;
    constexpr uint32_t kLegacyModePpgttEnable = 1 << 8;
    constexpr uint64_t kContextIdShift = 37;
    constexpr uint64_t kEngineClassShift = 61;
    constexpr uint64_t kEngineInstanceShift = 48;

    DASSERT(gpu_addr < (1ul << 32) && (gpu_addr & 0xFFF) == 0);
    DASSERT(context_id < (1u << 11));
    DASSERT(instance < (1u << 6));

    uint64_t desc = gpu_addr;
    desc |= kValid | kLegacyMode48bitPpgtt | kLegacyModePpgttEnable;
    desc |= static_cast<uint64_t>(context_id) << kContextIdShift;
    desc |= static_cast<uint64_t>(type) << kEngineClassShift;
    desc |= static_cast<uint64_t>(instance) << kEngineInstanceShift;
    return desc;
  }

  // May be expanded up to 8 descriptors at consecutive addresses.
  static void write(MsdIntelRegisterIo* reg_io, uint32_t mmio_base, uint64_t descriptor) {
    reg_io->Write32(magma::lower_32_bits(descriptor), mmio_base + kOffset);
    reg_io->Write32(magma::upper_32_bits(descriptor), mmio_base + kOffset + sizeof(uint32_t));
  }
};

// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf
// p.616
class ExeclistStatusGen9 {
 public:
  static constexpr uint32_t kOffset = 0x234;
  static constexpr uint32_t kExeclistCurrentPointerShift = 0;
  static constexpr uint32_t kExeclistWritePointerShift = 1;
  static constexpr uint32_t kExeclistQueueFullShift = 2;

  static uint64_t read(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    // Hmm 64-bit read would be better but kOffset is not 64bit aligned
    uint64_t status = reg_io->Read32(mmio_base + kOffset + 4);
    status = (status << 32) | reg_io->Read32(mmio_base + kOffset);
    return status;
  }

  static uint32_t execlist_current_pointer(uint64_t status) {
    return (status >> kExeclistCurrentPointerShift) & 0x1;
  }

  static uint32_t execlist_write_pointer(uint64_t status) {
    return (status >> kExeclistWritePointerShift) & 0x1;
  }

  static bool execlist_queue_full(uint64_t status) {
    return (status >> kExeclistQueueFullShift) & 0x1;
  }
};

// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.892
class ExeclistStatusGen12 : public hwreg::RegisterBase<ExeclistStatusGen12, uint64_t> {
 public:
  DEF_FIELD(63, 32, context_id);
  DEF_BIT(30, pending_load);
  DEF_BIT(27, arb_enable);
  DEF_FIELD(26, 12, last_context_switch_reason);
  DEF_FIELD(11, 8, active_context_offset);
  DEF_BIT(7, active_context);
  DEF_BIT(4, valid_exec_queue_dupe);
  DEF_BIT(3, valid_exec_queue);
  DEF_BIT(2, preempt_to_idle_pending);
  DEF_BIT(1, two_pending_loads);
  DEF_BIT(0, exec_queue_invalid);

  static auto GetAddr(uint32_t mmio_base) {
    return hwreg::RegisterAddr<ExeclistStatusGen12>(mmio_base + 0x234);
  }
};

// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.889
class ExeclistControl {
 public:
  static constexpr uint32_t kOffset = 0x550;
  static constexpr uint32_t kLoad = 1;

  static void load(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    reg_io->Write32(kLoad, mmio_base + kOffset);
  }
};

// intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part2_0.pdf p.748
class Timestamp {
 public:
  static constexpr uint32_t kOffset = 0x358;

  static uint64_t read(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    uint64_t upper = reg_io->Read32(mmio_base + kOffset + 4);
    uint64_t lower = reg_io->Read32(mmio_base + kOffset);
    uint64_t upper_check = reg_io->Read32(mmio_base + kOffset + 4);
    if (upper_check != upper) {
      // Assume rollover
      lower = reg_io->Read32(mmio_base + kOffset);
      upper = upper_check;
    }
    return (upper << 32 | lower);
  }
};

// from intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf p.1
class ActiveHeadPointer {
 public:
  static constexpr uint32_t kOffset = 0x74;
  static constexpr uint32_t kUpperOffset = 0x5C;

  static uint64_t read(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    uint64_t val = reg_io->Read32(mmio_base + kUpperOffset);
    val = (val << 32) | reg_io->Read32(mmio_base + kOffset);
    return val;
  }
};

class RingbufferHead {
 public:
  static constexpr uint32_t kOffset = 0x34;

  static uint32_t read(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    return reg_io->Read32(mmio_base + kOffset);
  }
};

// from intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf p.81
class AllEngineFault : public hwreg::RegisterBase<AllEngineFault, uint32_t> {
 public:
  DEF_FIELD(14, 12, engine);
  DEF_FIELD(10, 3, src);
  DEF_FIELD(2, 1, type);
  DEF_BIT(0, valid);

  enum Engine {
    RCS = 0,
    VCS1 = 1,
    VCS2 = 2,
    VBOX = 3,
    BLT = 4,
  };

  static auto GetAddr(uint32_t device_id) {
    if (DeviceId::is_gen12(device_id))
      return hwreg::RegisterAddr<AllEngineFault>(0xCEC4);

    DASSERT(DeviceId::is_gen9(device_id));
    return hwreg::RegisterAddr<AllEngineFault>(0x4094);
  }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.446
class FaultTlbReadData {
 public:
  static constexpr uint32_t kOffset0 = 0x4B10;
  static constexpr uint32_t kOffset1 = 0x4B14;
  static constexpr uint64_t kGgttCycle = 1ull << 36;

  static uint64_t read(MsdIntelRegisterIo* reg_io) {
    return (static_cast<uint64_t>(reg_io->Read32(kOffset1)) << 32) |
           static_cast<uint64_t>(reg_io->Read32(kOffset0));
  }

  static uint64_t addr(uint64_t val) { return (val & 0xFFFFFFFFFull) << 12; }
  static bool is_ggtt(uint64_t val) { return val & kGgttCycle; }
};

class PowerGateEnable : public hwreg::RegisterBase<PowerGateEnable, uint32_t> {
 public:
  DEF_BIT(0, render_powergate_enable);
  DEF_BIT(1, media_powergate_enable);
  DEF_BIT(2, media_sampler_powergate_enable);
  DEF_BIT(3, vcs0_hcp_powergate_enable);
  DEF_BIT(4, vcs0_mfx_powergate_enable);
  DEF_BIT(5, vcs1_hcp_powergate_enable);
  DEF_BIT(6, vcs1_mfx_powergate_enable);
  DEF_BIT(7, vcs2_hcp_powergate_enable);
  DEF_BIT(8, vcs2_mfx_powergate_enable);
  // More available hcp/mfx bits up to vcs7

  static constexpr uint32_t kPowerGateAll = 0xFFFF'FFFF;

  static auto GetAddr() { return hwreg::RegisterAddr<PowerGateEnable>(0xA210); }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.493
class ForceWake {
 public:
  enum Domain { RENDER, GEN9_MEDIA, GEN12_VDBOX0 };

  static constexpr uint32_t kRenderOffset = 0xA278;
  static constexpr uint32_t kRenderStatusOffset = 0xD84;

  static constexpr uint32_t kGen9MediaOffset = 0xA270;
  static constexpr uint32_t kGen9MediaStatusOffset = 0xD88;

  static constexpr uint32_t kGen12Vdbox0Offset = 0xA540;
  static constexpr uint32_t kGen12Vdbox0StatusOffset = 0xD50;

  static void reset(MsdIntelRegisterIo* reg_io, Domain domain) { write(reg_io, domain, 0xFFFF, 0); }

  static void write(MsdIntelRegisterIo* reg_io, Domain domain, uint16_t mask, uint16_t val) {
    uint32_t val32 = mask;
    val32 = (val32 << 16) | val;
    switch (domain) {
      case RENDER:
        reg_io->Write32(val32, kRenderOffset);
        break;
      case GEN9_MEDIA:
        reg_io->Write32(val32, kGen9MediaOffset);
        break;
      case GEN12_VDBOX0:
        reg_io->Write32(val32, kGen12Vdbox0Offset);
        break;
    }
  }

  static uint16_t read_status(MsdIntelRegisterIo* reg_io, Domain domain) {
    switch (domain) {
      case RENDER:
        return static_cast<uint16_t>(reg_io->Read32(kRenderStatusOffset));
      case GEN9_MEDIA:
        return static_cast<uint16_t>(reg_io->Read32(kGen9MediaStatusOffset));
      case GEN12_VDBOX0:
        return static_cast<uint16_t>(reg_io->Read32(kGen12Vdbox0StatusOffset));
    }
  }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.618
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1034
class GraphicsMode {
 public:
  static constexpr uint32_t kOffset = 0x29C;
  static constexpr uint32_t kExeclistEnableGen9 = 1 << 15;
  static constexpr uint32_t kExeclistDisableLegacyGen11 = 1 << 3;

  static uint32_t read(MsdIntelRegisterIo* reg_io, uint32_t mmio_base) {
    return reg_io->Read32(mmio_base + kOffset);
  }

  static void write(MsdIntelRegisterIo* reg_io, uint32_t mmio_base, uint16_t mask, uint16_t val) {
    uint32_t val32 = mask;
    val32 = (val32 << 16) | val;
    reg_io->Write32(val32, mmio_base + kOffset);
    reg_io->mmio()->PostingRead32(mmio_base + kOffset);
  }
};

// from Intel-GFX-BSpec-NDA-SKL-20150707-b93797-r96240-Web register spec
class RenderPerformanceNormalFrequencyRequest {
 public:
  static constexpr uint32_t kOffset = 0xA008;

  static void write_frequency_request_gen9(MsdIntelRegisterIo* reg_io, uint32_t mhz) {
    // Register in units of 16.66Mhz on skylake
    uint32_t val = mhz * 3 / 50;
    DASSERT(val <= 0x1ff);
    reg_io->Write32(val << 23, kOffset);
  }

  static uint32_t read(MsdIntelRegisterIo* reg_io) {
    // Register in units of 16.66Mhz on skylake
    return ((reg_io->Read32(kOffset) >> 23) & 0x1ff) * 50 / 3;
  }
};

class RenderPerformanceStatus {
 public:
  static constexpr uint32_t kOffset = 0xA01C;

  // Returns frequency in MHz
  static uint32_t read_current_frequency_gen9(MsdIntelRegisterIo* reg_io) {
    // Register in units of 16.66Mhz on skylake
    return (reg_io->Read32(kOffset) >> 23) * 50 / 3;
  }
};

class RenderPerformanceStateCapability {
 public:
  static constexpr uint32_t kOffset = 0x140000 + 0x5998;

  // Returns frequency in Mhz
  static uint32_t read_rp0_frequency(MsdIntelRegisterIo* register_io) {
    // Register units are 50Mhz
    return (register_io->Read32(kOffset) & 0xff) * 50;
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.741
class ResetControl {
 public:
  static constexpr uint32_t kOffset = 0xd0;
  static constexpr uint32_t kRequestResetBit = 0;
  static constexpr uint32_t kReadyForResetBit = 1;

  static void request(MsdIntelRegisterIo* register_io, uint32_t mmio_base) {
    register_io->Write32(((1 << kRequestResetBit) << 16) | (1 << kRequestResetBit),
                         mmio_base + kOffset);
  }

  static bool ready_for_reset(MsdIntelRegisterIo* register_io, uint32_t mmio_base) {
    return register_io->Read32(mmio_base + kOffset) & (1 << kReadyForResetBit);
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.755
class GraphicsDeviceResetControl {
 public:
  static constexpr uint32_t kOffset = 0x941C;
  static constexpr uint32_t kRcsResetBit = 1;
  static constexpr uint32_t kVcsResetBit = 2;
  static constexpr uint32_t kVcs0ResetBitGen12 = 5;

  static void initiate_reset(MsdIntelRegisterIo* register_io, uint8_t bit) {
    DASSERT(bit == kRcsResetBit || bit == kVcsResetBit || bit == kVcs0ResetBitGen12);
    register_io->Write32((1 << bit), kOffset);
  }

  static bool is_reset_complete(MsdIntelRegisterIo* register_io, uint8_t bit) {
    DASSERT(bit == kRcsResetBit || bit == kVcsResetBit || bit == kVcs0ResetBitGen12);
    return (register_io->Read32(kOffset) & (1 << bit)) == 0;
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.10
class MasterInterruptControl {
 public:
  static constexpr uint32_t kOffset = 0x44200;
  static constexpr uint32_t kRenderInterruptsPendingBitMask = 1 << 0;
  static constexpr uint32_t kVideoInterruptsPendingBitMask = 1 << 2;
  static constexpr uint32_t kDisplayEnginePipeAInterruptsPendingBit = 1 << 16;
  static constexpr uint32_t kEnableBitMask = 1 << 31;

  static void write(MsdIntelRegisterIo* register_io, bool enable) {
    register_io->Write32(enable ? kEnableBitMask : 0, kOffset);
  }
  static uint32_t read(MsdIntelRegisterIo* register_io) { return register_io->Read32(kOffset); }
};

class InterruptRegisterBase {
 public:
  enum MaskOp { MASK, UNMASK };

  static constexpr uint32_t kUserBit = 1 << 0;
  static constexpr uint32_t kPageFaultBit = 1 << 7;  // Only for the Interrupt0 register.
  static constexpr uint32_t kContextSwitchBit = 1 << 8;

  static void write(MsdIntelRegisterIo* register_io, uint32_t offset, bool set,
                    uint16_t upper_engine_bits, uint16_t lower_engine_bits) {
    DASSERT(((upper_engine_bits | lower_engine_bits) & ~(kUserBit | kContextSwitchBit)) == 0);
    uint32_t bits = upper_engine_bits;
    bits = (bits << 16) | lower_engine_bits;

    uint32_t val = register_io->Read32(offset);

    val = set ? (val | bits) : (val & ~bits);
    register_io->Write32(val, offset);

    register_io->mmio()->PostingRead32(offset);
  }
};

class HardwareStatusMask : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x98;

  static void write(MsdIntelRegisterIo* register_io, uint32_t mmio_base, MaskOp op, uint16_t bits) {
    InterruptRegisterBase::write(register_io, mmio_base + kOffset, op == MASK,
                                 /*upper_engine_bits=*/0, bits);
  }
};

// For GtInterrupt0 series the source bits correspond to RenderCS
// (BlitterCS in the upper 16 bits).
// intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf p.924
class GtInterruptMask0 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x44304;

  static void mask_render(MsdIntelRegisterIo* register_io, MaskOp op, uint16_t bits) {
    InterruptRegisterBase::write(register_io, kOffset, op == MASK, /*upper_engine_bits=*/0, bits);
  }
};

class GtInterruptIdentity0 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x44308;

  static uint32_t read(MsdIntelRegisterIo* register_io) { return register_io->Read32(kOffset); }

  static void clear(MsdIntelRegisterIo* register_io, uint16_t bits) {
    DASSERT((bits & ~(kUserBit | kContextSwitchBit)) == 0);
    register_io->Write32(bits, kOffset);
  }
};

class GtInterruptEnable0 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x4430C;

  static void enable_render(MsdIntelRegisterIo* register_io, bool enable, uint16_t bits) {
    InterruptRegisterBase::write(register_io, kOffset, enable, /*upper_engine_bits=*/0, bits);
  }
};

// For GtInterrupt1 series the source bits correspond to VideoCS
// (VideoCS2 in the upper 16 bits).
// intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf p.926
class GtInterruptMask1 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x44314;

  static void mask_vcs0(MsdIntelRegisterIo* register_io, MaskOp op, uint16_t bits) {
    InterruptRegisterBase::write(register_io, kOffset, op == MASK, /*upper_engine_bits=*/0, bits);
  }
};

class GtInterruptIdentity1 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x44318;

  static uint32_t read(MsdIntelRegisterIo* register_io) { return register_io->Read32(kOffset); }

  static void clear(MsdIntelRegisterIo* register_io, uint16_t bits) {
    DASSERT((bits & ~(kUserBit | kContextSwitchBit)) == 0);
    register_io->Write32(bits, kOffset);
  }
};

class GtInterruptEnable1 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x4431C;

  static void enable_vcs0(MsdIntelRegisterIo* register_io, bool enable, uint16_t bits) {
    InterruptRegisterBase::write(register_io, kOffset, enable, /*upper_engine_bits=*/0, bits);
  }
};

// GT_ENG_INTR_ENABLE
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1077
//  Upper: render, lower: copy (blitter)
class GtInterruptEnable0Gen12 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x190030;

  static void enable_render(MsdIntelRegisterIo* register_io, bool enable, uint16_t bits) {
    write(register_io, kOffset, enable, bits, /*lower_engine_bits=*/0);
  }
};

// GT_ENG_INTR_ENABLE
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1077
//  Upper: video decode, lower: video enhance
class GtInterruptEnable1Gen12 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x190034;

  static void enable_video_decode(MsdIntelRegisterIo* register_io, bool enable, uint16_t bits) {
    write(register_io, kOffset, enable, bits, /*lower_engine_bits=*/0);
  }
};

// GT_ENG_INTR_MASK
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1079
//  Upper: render, lower (reserved)
class GtInterruptMask0Gen12 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x190090;

  static void mask_render(MsdIntelRegisterIo* register_io, MaskOp op, uint16_t bits) {
    write(register_io, kOffset, op == MASK, bits, /*lower_engine_bits=*/0);
  }
};

// GT_ENG_INTR_MASK
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1079
//  Upper: vcs0, lower: vcs1
class GtInterruptMask2Gen12 : public InterruptRegisterBase {
 public:
  static constexpr uint32_t kOffset = 0x1900A8;

  static void mask_vcs0(MsdIntelRegisterIo* register_io, MaskOp op, uint16_t bits) {
    write(register_io, kOffset, op == MASK, bits, /*lower_engine_bits=*/0);
  }
};

// GT_INTR_DW0
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1088
class GtInterruptStatus0Gen12 : public hwreg::RegisterBase<GtInterruptStatus0Gen12, uint32_t> {
 public:
  DEF_BIT(0, rcs0);

  static GtInterruptStatus0Gen12 Get(MsdIntelRegisterIo* reg_io) {
    return hwreg::RegisterAddr<GtInterruptStatus0Gen12>(0x190018).ReadFrom(reg_io);
  }
};

// GT_INTR_DW1
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1090
class GtInterruptStatus1Gen12 : public hwreg::RegisterBase<GtInterruptStatus1Gen12, uint32_t> {
 public:
  DEF_BIT(0, vcs0);

  static GtInterruptStatus1Gen12 Get(MsdIntelRegisterIo* reg_io) {
    return hwreg::RegisterAddr<GtInterruptStatus1Gen12>(0x19001C).ReadFrom(reg_io);
  }
};

// GT_INTR_IIR_SELECTOR
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1092
class GtInterruptSelector0Gen12 {
 public:
  static constexpr uint32_t kOffset = 0x190070;
  static constexpr uint32_t kRcs0Bit = 0x1;

  static void write_rcs0(MsdIntelRegisterIo* register_io) {
    register_io->Write32(kRcs0Bit, kOffset);
  }
};

// GT_INTR_IIR_SELECTOR
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1092
class GtInterruptSelector1Gen12 {
 public:
  static constexpr uint32_t kOffset = 0x190074;
  static constexpr uint32_t kVcs0Bit = 0x1;

  static void write_vcs0(MsdIntelRegisterIo* register_io) {
    register_io->Write32(kVcs0Bit, kOffset);
  }
};

// GT_INTR_IDENTITY
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
// p.1091
class GtInterruptIdentityGen12 : public hwreg::RegisterBase<GtInterruptIdentityGen12, uint32_t> {
 public:
  DEF_BIT(31, data_valid);
  DEF_FIELD(25, 20, instance_id);
  DEF_FIELD(18, 16, class_id);
  DEF_FIELD(15, 0, interrupt);

  bool SpinUntilValid(MsdIntelRegisterIo* register_io, std::chrono::microseconds timeout) {
    auto start = std::chrono::steady_clock::now();

    while (data_valid() == 0) {
      ReadFrom(register_io);
      if (std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start) >=
          timeout) {
        break;
      }
    }

    return data_valid();
  }

  void Clear(MsdIntelRegisterIo* register_io) {
    set_reg_value(0).set_data_valid(1);
    WriteTo(register_io);
  }

  static auto GetBank0(MsdIntelRegisterIo* reg_io) {
    return hwreg::RegisterAddr<GtInterruptIdentityGen12>(0x190060).ReadFrom(reg_io);
  }
  static auto GetBank1(MsdIntelRegisterIo* reg_io) {
    return hwreg::RegisterAddr<GtInterruptIdentityGen12>(0x190064).ReadFrom(reg_io);
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.764
class MemoryObjectControlState {
 public:
  static constexpr uint32_t kGraphicsOffset = 0xC800;
  static constexpr uint32_t kGlobalOffsetGen12 = 0x4000;

  static constexpr uint32_t kCacheabilityShift = 0;
  static constexpr uint32_t kCacheShift = 2;
  static constexpr uint32_t kLruManagementShift = 4;

  enum Cacheability { PAGETABLE = 0, UNCACHED, WRITETHROUGH, WRITEBACK };
  enum Cache { LLC = 1, LLC_ELLC = 2 };
  enum LruManagement { LRU_0 = 0, LRU_3 = 3 };

  static constexpr uint32_t format(Cacheability cacheability, Cache cache,
                                   LruManagement lru_management) {
    return (lru_management << kLruManagementShift) | (cache << kCacheShift) |
           (cacheability << kCacheabilityShift);
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.1118
class LncfMemoryObjectControlState {
 public:
  static constexpr uint32_t kOffset = 0xB020;

  static constexpr uint32_t kCacheabilityShift = 4;

  enum Cacheability { DIRECT = 0, UNCACHED, WRITETHROUGH, WRITEBACK };

  static constexpr uint16_t format(Cacheability cacheability) {
    return static_cast<uint16_t>(cacheability << kCacheabilityShift);
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.403
class Fuse2ControlDwordMirror {
 public:
  static constexpr uint32_t kOffset = 0x9120;

  static constexpr uint32_t kSliceEnableShift = 25;
  static constexpr uint32_t kSliceEnableMask = 0x7 << kSliceEnableShift;
  static constexpr uint32_t kSubsliceDisableShift = 20;
  static constexpr uint32_t kSubsliceDisableMask = 0xf << kSubsliceDisableShift;

  static void read(MsdIntelRegisterIo* register_io, uint32_t* slice_enable_mask_out,
                   uint32_t* subslice_enable_mask_out) {
    uint32_t val = register_io->Read32(kOffset);
    *slice_enable_mask_out = (val & kSliceEnableMask) >> kSliceEnableShift;
    *subslice_enable_mask_out = ((~val) & kSubsliceDisableMask) >> kSubsliceDisableShift;
  }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.398
class MirrorEuDisable {
 public:
  static constexpr uint32_t kOffset = 0x9134;

  static constexpr uint32_t kMaxSliceCount = 3;
  static constexpr uint32_t kMaxSubsliceCount = 4;
  static constexpr uint32_t kEuPerSubslice = 8;
  static constexpr uint32_t kSubsliceMask = 0xff;

  static_assert(kMaxSubsliceCount * kEuPerSubslice == sizeof(uint32_t) * 8,
                "eu/subslice math is wrong");
  static_assert(kSubsliceMask == (1 << kEuPerSubslice) - 1, "wrong kSubsliceMask");

  static void read(MsdIntelRegisterIo* register_io, uint8_t slice,
                   std::vector<uint32_t>& eu_disable_mask_out) {
    DASSERT(slice < kMaxSliceCount);
    uint32_t val = register_io->Read32(kOffset + slice * sizeof(uint32_t));

    eu_disable_mask_out.clear();

    for (uint32_t subslice = 0; subslice < kMaxSubsliceCount; subslice++) {
      eu_disable_mask_out.push_back(val & kSubsliceMask);
      val >>= kEuPerSubslice;
    }
  }
};

// MIRROR_EU_DISABLE0
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part2-rev2_1.pdf
// p.81
class MirrorEuDisableGen12 {
 public:
  static constexpr uint32_t kOffset = 0x9134;

  static constexpr uint32_t kEusPerSubslice = 16;
  static constexpr uint32_t kEuDisableBits = kEusPerSubslice / 2;
  static constexpr uint32_t kEuDisableBitMask = (1 << kEuDisableBits) - 1;

  // EU disable bits are the same for every subslice.
  static std::bitset<kEuDisableBits> read(MsdIntelRegisterIo* register_io) {
    uint32_t val = register_io->Read32(kOffset);

    return std::bitset<kEuDisableBits>(val & kEuDisableBitMask);
  }
};

// GEN12_GT_GEOMETRY_DSS_ENABLE
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part2-rev2_1.pdf
// p.97
class MirrorDssEnable {
 public:
  static constexpr uint32_t kOffset = 0x913C;

  static constexpr uint32_t kDssPerSlice = 6;
  static constexpr uint32_t kDssEnableMask = (1 << kDssPerSlice) - 1;
  static constexpr uint32_t kMaxSlice = 32 / kDssPerSlice;

  // Reads the dual-subslice enable masks.
  static std::vector<std::bitset<kDssPerSlice>> read(MsdIntelRegisterIo* register_io) {
    uint32_t val = register_io->Read32(kOffset);

    std::vector<std::bitset<kDssPerSlice>> dss_enable_masks;

    for (uint32_t i = 0; i < kMaxSlice; i++) {
      dss_enable_masks.push_back((val >> (i * kDssPerSlice)) & kDssEnableMask);
    }

    return dss_enable_masks;
  }
};

// PWR_WELL_CTL: Power well control.  This allows enabling or disabling
// power to various "power wells" (groups of functional units).
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class PowerWellControl2 : public hwreg::RegisterBase<PowerWellControl2, uint32_t> {
 public:
  DEF_BIT(31, power_well_2_request);
  DEF_BIT(30, power_well_2_state);
  DEF_BIT(29, power_well_1_request);
  DEF_BIT(28, power_well_1_state);
  DEF_BIT(9, ddi_d_io_power_request);
  DEF_BIT(8, ddi_d_io_power_state);
  DEF_BIT(7, ddi_c_io_power_request);
  DEF_BIT(6, ddi_c_io_power_state);
  DEF_BIT(5, ddi_b_io_power_request);
  DEF_BIT(4, ddi_b_io_power_state);
  DEF_BIT(3, ddi_a_and_e_io_power_request);
  DEF_BIT(2, ddi_a_and_e_io_power_state);
  DEF_BIT(1, misc_io_power_request);
  DEF_BIT(0, misc_io_power_state);

  static auto Get() { return hwreg::RegisterAddr<PowerWellControl2>(0x45404); }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.86
class ArbiterControl {
 public:
  static constexpr uint32_t kOffset = 0xb004;
  static constexpr uint32_t kGapsTsvCreditFixEnable = 1 << 7;

  static void workaround(MsdIntelRegisterIo* register_io) {
    uint32_t value = register_io->Read32(kOffset) | kGapsTsvCreditFixEnable;
    register_io->Write32(value, kOffset);
  }
};

class RenderEngineTlbControl : public hwreg::RegisterBase<RenderEngineTlbControl, uint32_t> {
 public:
  DEF_BIT(0, invalidate);
  static auto Get() { return hwreg::RegisterAddr<RenderEngineTlbControl>(0x4260); }
};

class VideoEngineTlbControl : public hwreg::RegisterBase<VideoEngineTlbControl, uint32_t> {
 public:
  DEF_BIT(0, invalidate);
  static auto Get() { return hwreg::RegisterAddr<VideoEngineTlbControl>(0x4264); }
};

class CacheMode1 {
 public:
  static constexpr uint32_t kOffset = 0x7004;
  static constexpr uint32_t k4x4StcOptimizationDisable = 1 << 6;
  static constexpr uint32_t kPartialResolveInVcDisable = 1 << 1;
};

class RegisterOffset7300 {
 public:
  static constexpr uint32_t kOffset = 0x7300;
  static constexpr uint16_t kWaForceEnableNonCoherent = 1 << 4;
};

}  // namespace registers

#endif  // REGISTERS_H
