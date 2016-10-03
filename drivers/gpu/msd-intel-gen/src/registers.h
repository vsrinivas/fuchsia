// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "register_io.h"
#include "types.h"

namespace registers {

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.733
class GmchGraphicsControl {
public:
    static constexpr uint32_t kOffset = 0x50;
    static constexpr uint32_t kGttSizeShift = 6;
    static constexpr uint32_t kGttSizeMask = 0x3;

    static uint32_t gtt_size(uint32_t val)
    {
        unsigned int size = (val >> kGttSizeShift) & kGttSizeMask;
        return (size == 0) ? 0 : (1 << size) * 1024 * 1024;
    }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.712
class HardwareStatusPageAddress {
public:
    static constexpr uint32_t kOffset = 0x80;

    static void write(RegisterIo* reg_io, uint64_t mmio_base, uint32_t addr)
    {
        reg_io->Write32(mmio_base + kOffset, addr);
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

    static void write(RegisterIo* reg_io, uint64_t val)
    {
        reg_io->Write32(kOffsetLow, static_cast<uint32_t>(val));
        reg_io->Write32(kOffsetHigh, static_cast<uint32_t>(val >> 32));
    }

    static uint64_t ppat(unsigned int index, uint8_t lru_age, uint8_t target_cache,
                         uint8_t mem_type)
    {
        DASSERT((lru_age & ~kLruAgeMask) == 0);
        DASSERT((target_cache & ~kTargetCacheMask) == 0);
        DASSERT((mem_type & ~kMemTypeMask) == 0);
        uint64_t ppat = (lru_age << 4) | (target_cache << 2) | mem_type;
        return ppat << (index * 8);
    }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.438
// and intel-gfx-prm-osrc-bdw-vol02d-commandreference-structures_3.pdf p.107
class ExeclistSubmitPort {
public:
    static constexpr uint32_t kSubmitOffset = 0x230;
    static constexpr uint32_t kStatusOffset = 0x234;

    static uint64_t context_descriptor(gpu_addr_t gpu_addr, uint32_t context_id, bool ppgtt_enable)
    {
        constexpr uint32_t kValid = 1;
        constexpr uint32_t kLegacyMode32bitPpgtt = 1 << 3;
        constexpr uint32_t kL3llcCoherent = 1 << 5;
        constexpr uint32_t kLegacyModePpgttEnable = 1 << 8;
        constexpr uint32_t kContextIdShift = 32;

        uint64_t desc = gpu_addr;
        desc |= kValid;
        desc |= kLegacyMode32bitPpgtt;
        desc |= kL3llcCoherent;
        if (ppgtt_enable)
            desc |= kLegacyModePpgttEnable;
        desc |= static_cast<uint64_t>(context_id) << kContextIdShift;
        return desc;
    }

    static void write(RegisterIo* reg_io, uint64_t mmio_base, uint64_t descriptor1,
                      uint64_t descriptor0)
    {
        uint32_t desc[4]{magma::upper_32_bits(descriptor1), magma::lower_32_bits(descriptor1),
                         magma::upper_32_bits(descriptor0), magma::lower_32_bits(descriptor0)};

        // The last write triggers the context load.
        reg_io->Write32(mmio_base + kSubmitOffset, desc[0]);
        reg_io->Write32(mmio_base + kSubmitOffset, desc[1]);
        reg_io->Write32(mmio_base + kSubmitOffset, desc[2]);
        reg_io->Write32(mmio_base + kSubmitOffset, desc[3]);

        reg_io->mmio()->PostingRead32(mmio_base + kStatusOffset);
    }
};

class ActiveHeadPointer {
public:
    static constexpr uint32_t kOffset = 0x74;
    static constexpr uint32_t kUpperOffset = 0x5C;

    static uint64_t read(RegisterIo* reg_io, uint64_t mmio_base)
    {
        uint64_t val = reg_io->Read32(mmio_base + kUpperOffset);
        val = (val << 32) | reg_io->Read32(mmio_base + kOffset);
        return val;
    }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.75
class AllEngineFault {
public:
    static constexpr uint32_t kOffset = 0x4094;
    static constexpr uint32_t kValid = 1;
    static constexpr uint32_t kEngineShift = 12;
    static constexpr uint32_t kEngineMask = 0x3;
    static constexpr uint32_t kSrcShift = 3;
    static constexpr uint32_t kSrcMask = 0xFF;
    static constexpr uint32_t kTypeShift = 1;
    static constexpr uint32_t kTypeMask = 0x3;

    static uint32_t read(RegisterIo* reg_io) { return reg_io->Read32(kOffset); }
    static void clear(RegisterIo* reg_io) { reg_io->Write32(kOffset, 0); }

    static bool valid(uint32_t val) { return val & kValid; }
    static uint32_t engine(uint32_t val) { return (val >> kEngineShift) & kEngineMask; }
    static uint32_t src(uint32_t val) { return (val >> kSrcShift) & kSrcMask; }
    static uint32_t type(uint32_t val) { return (val >> kTypeShift) & kTypeMask; }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.446
class FaultTlbReadData {
public:
    static constexpr uint32_t kOffset0 = 0x4B10;
    static constexpr uint32_t kOffset1 = 0x4B14;
    static constexpr uint32_t kGgttCycle = 1 << 4;

    static uint64_t addr(RegisterIo* reg_io)
    {
        return (static_cast<uint64_t>(reg_io->Read32(kOffset1) & 0xF) << 44) |
               (static_cast<uint64_t>(reg_io->Read32(kOffset0)) << 12);
    }

    static bool is_ggtt(RegisterIo* reg_io) { return reg_io->Read32(kOffset1) & kGgttCycle; }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.493
class ForceWake {
public:
    enum Domain { GEN8, GEN9_RENDER };

    static constexpr uint32_t kOffset = 0xA188;
    static constexpr uint32_t kStatusOffset = 0x130044;

    static constexpr uint32_t kRenderOffset = 0xA278;
    static constexpr uint32_t kRenderStatusOffset = 0xD84;

    static void reset(RegisterIo* reg_io, Domain domain) { write(reg_io, domain, 0xFFFF, 0); }

    static void write(RegisterIo* reg_io, Domain domain, uint16_t mask, uint16_t val)
    {
        uint32_t val32 = mask;
        val32 = (val32 << 16) | val;
        switch (domain) {
        case GEN8:
            reg_io->Write32(kOffset, val32);
            break;
        case GEN9_RENDER:
            reg_io->Write32(kRenderOffset, val32);
            break;
        }
    }

    static uint16_t read_status(RegisterIo* reg_io, Domain domain)
    {
        switch (domain) {
        case GEN8:
            return static_cast<uint16_t>(reg_io->Read32(kStatusOffset));
        case GEN9_RENDER:
            return static_cast<uint16_t>(reg_io->Read32(kRenderStatusOffset));
        }
    }
};

// from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.618
class GraphicsMode {
public:
    static constexpr uint32_t kOffset = 0x29C;
    static constexpr uint32_t kExeclistEnable = 1 << 15;

    static void write(RegisterIo* reg_io, uint64_t mmio_base, uint16_t mask, uint16_t val)
    {
        uint32_t val32 = mask;
        val32 = (val32 << 16) | val;
        reg_io->Write32(mmio_base + kOffset, val32);
        reg_io->mmio()->PostingRead32(mmio_base + kOffset);
    }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.601
class DisplayPlaneSurfaceAddress {
public:
    enum Plane { PIPE_A_PLANE_1 };

    static constexpr uint32_t kOffsetPipeAPlane1 = 0x7019C;

    static uint32_t read(RegisterIo* reg_io, Plane plane)
    {
        switch (plane) {
        case PIPE_A_PLANE_1:
            return reg_io->Read32(kOffsetPipeAPlane1);
        }
    }

    static void write(RegisterIo* reg_io, Plane plane, uint32_t gpu_addr_gtt)
    {
        switch (plane) {
        case PIPE_A_PLANE_1:
            reg_io->Write32(kOffsetPipeAPlane1, gpu_addr_gtt);
            break;
        }
    }
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.444
class DisplayPipeInterrupt {
public:
    enum Pipe { PIPE_A };

    static constexpr uint32_t kMaskOffsetPipeA = 0x44404;
    static constexpr uint32_t kIdentityOffsetPipeA = 0x44408;
    static constexpr uint32_t kPlane1FlipDoneBit = 1 << 3;

    static void update_mask_bits(RegisterIo* reg_io, Pipe pipe, uint32_t bits, bool enable)
    {
        uint32_t offset, val;
        switch (pipe) {
        case PIPE_A:
            offset = kMaskOffsetPipeA;
            break;
        }

        val = reg_io->Read32(offset);
        val = enable ? (val & ~bits) : val | bits;
        reg_io->Write32(offset, val);
    }

    static void process_identity_bits(RegisterIo* reg_io, Pipe pipe, uint32_t bits,
                                      bool* bits_present_out)
    {
        uint32_t offset, val;
        switch (pipe) {
        case PIPE_A:
            offset = kIdentityOffsetPipeA;
            break;
        }
        val = reg_io->Read32(offset);
        if ((*bits_present_out = val & bits))
            reg_io->Write32(offset, val | bits); // reset the event
    }
};

// from Intel-GFX-BSpec-NDA-SKL-20150707-b93797-r96240-Web register spec
class RenderPerformanceNormalFrequencyRequest {
public:
    static constexpr uint32_t kOffset = 0xA008;

    static void write_frequency_request(RegisterIo* reg_io, uint32_t val)
    {
        return reg_io->Write32(kOffset, val << 23);
    }
};

class RenderPerformanceStatus {
public:
    static constexpr uint32_t kOffset = 0xA01C;

    // Returns frequency in MHz
    static uint32_t read_current_frequency_gen9(RegisterIo* reg_io)
    {
        return (reg_io->Read32(kOffset) >> 23) * 50;
    }
};

class RenderPerformanceStateCapability {
public:
    static constexpr uint32_t kOffset = 0x140000 + 0x5998;

    static uint32_t read_rp0_frequency(RegisterIo* register_io)
    {
        return register_io->Read32(kOffset) & 0xff;
    }
};

} // namespace

#endif // REGISTERS_H
