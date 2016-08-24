// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "register_io.h"

namespace registers {

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

} // namespace

#endif // REGISTERS_H
