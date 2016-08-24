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

    static void write_ringbuffer(Ringbuffer* ringbuffer, gpu_addr_t gpu_addr, bool ppgtt)
    {
        ringbuffer->write_tail(kCommandType | (ppgtt ? kAddressSpacePpgtt : 0));
        ringbuffer->write_tail(magma::lower_32_bits(gpu_addr));
        ringbuffer->write_tail(magma::upper_32_bits(gpu_addr));
    }
};

// from intel-gfx-prm-osrc-bdw-vol02a-commandreference-instructions_2.pdf pp.918
class MiStoreDataImmediate {
public:
    static constexpr uint32_t kDwordCount = 4;
    static constexpr uint32_t kCommandType = 0x20 << 23;
    static constexpr uint32_t kAddressSpaceGtt = 1 << 22;

    static void write_ringbuffer(Ringbuffer* ringbuffer, uint32_t dword, gpu_addr_t gpu_addr,
                                 bool gtt)
    {
        ringbuffer->write_tail(kCommandType | (kDwordCount - 2) | (gtt ? kAddressSpaceGtt : 0));
        ringbuffer->write_tail(magma::lower_32_bits(gpu_addr));
        ringbuffer->write_tail(magma::upper_32_bits(gpu_addr));
        ringbuffer->write_tail(dword);
    }
};

#endif // INSTRUCTIONS_H
