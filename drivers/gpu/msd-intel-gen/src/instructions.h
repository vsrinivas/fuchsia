// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include "ringbuffer.h"

// from intel-gfx-prm-osrc-bdw-vol02a-commandreference-instructions_2.pdf pp.870
class MiNoop {
public:
    static constexpr uint32_t kDwordCount = 1;
    static constexpr uint32_t kCommandType = 0;

    static void write_ringbuffer(Ringbuffer* ringbuffer) { ringbuffer->write_tail(kCommandType); }
};

// from intel-gfx-prm-osrc-bdw-vol02a-commandreference-instructions_2.pdf pp.793
class MiBatchBufferStart {
public:
    static constexpr uint32_t kDwordCount = 3;
    static constexpr uint32_t kCommandType = 0x31 << 23;
    static constexpr uint32_t kAddressSpacePpgtt = 1 << 8;

    static void write_ringbuffer(Ringbuffer* ringbuffer, gpu_addr_t gpu_addr,
                                 AddressSpaceType address_space_type)
    {
        ringbuffer->write_tail(
            kCommandType | (kDwordCount - 2) |
            (address_space_type == ADDRESS_SPACE_PPGTT ? kAddressSpacePpgtt : 0));
        ringbuffer->write_tail(magma::lower_32_bits(gpu_addr));
        ringbuffer->write_tail(magma::upper_32_bits(gpu_addr));
    }
};

// intel-gfx-prm-osrc-skl-vol02a-commandreference-instructions.pdf pp.1057
class MiPipeControl {
public:
    static constexpr uint32_t kDwordCount = 6;
    static constexpr uint32_t kCommandType = 0x3 << 29;
    static constexpr uint32_t kCommandSubType = 0x3 << 27;
    static constexpr uint32_t k3dCommandOpcode = 0x2 << 24;
    static constexpr uint32_t k3dCommandSubOpcode = 0 << 16;

    static constexpr uint32_t kIndirectStatePointersDisableBit = 1 << 9;
    static constexpr uint32_t kPostSyncWriteImmediateBit = 1 << 14;
    static constexpr uint32_t kCommandStreamerStallEnableBit = 1 << 20;
    static constexpr uint32_t kAddressSpaceGlobalGttBit = 1 << 24;

    static void write(Ringbuffer* ringbuffer, uint32_t sequence_number, uint64_t gpu_addr,
                      uint32_t flags)
    {
        DASSERT((flags & ~(kCommandStreamerStallEnableBit | kIndirectStatePointersDisableBit)) ==
                0);
        ringbuffer->write_tail(kCommandType | kCommandSubType | k3dCommandOpcode |
                               k3dCommandSubOpcode | (kDwordCount - 2));
        ringbuffer->write_tail(flags | kPostSyncWriteImmediateBit | kAddressSpaceGlobalGttBit);
        ringbuffer->write_tail(magma::lower_32_bits(gpu_addr));
        ringbuffer->write_tail(magma::upper_32_bits(gpu_addr));
        ringbuffer->write_tail(sequence_number);
        ringbuffer->write_tail(0);
    }
};

// intel-gfx-prm-osrc-skl-vol02a-commandreference-instructions.pdf pp.1010
class MiUserInterrupt {
public:
    static constexpr uint32_t kDwordCount = 1;
    static constexpr uint32_t kCommandType = 0x2 << 23;

    static void write(Ringbuffer* ringbuffer) { ringbuffer->write_tail(kCommandType); }
};

#endif // INSTRUCTIONS_H
