// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODESET_DISPLAYPORT_H
#define MODESET_DISPLAYPORT_H

#include "magma_util/macros.h"
#include "register_io.h"

// This represents a message sent over DisplayPort's Aux channel, including
// reply messages.
struct DpAuxMessage {
    // Sizes in bytes.  DisplayPort Aux messages are quite small.
    static constexpr uint32_t kMaxTotalSize = 20;
    static constexpr uint32_t kMaxBodySize = 16;

    uint8_t data[kMaxTotalSize];
    uint32_t size;

    // The Intel hardware's registers want the 32-bit words of the
    // DisplayPort Aux message in big-endian format, which is a little odd.
    // GetPackedWord() and SetFromPackedWord() convert to and from that
    // format.
    //
    // Note that GetPackedWord() avoids reading any uninitialized or
    // leftover data beyond |size|.
    uint32_t GetPackedWord(uint32_t offset) const
    {
        DASSERT(offset % 4 == 0);
        uint32_t limit = size - offset;
        uint32_t result = 0;
        for (uint32_t i = 0; i < 4 && i < limit; ++i) {
            result |= static_cast<uint32_t>(data[offset + i]) << ((3 - i) * 8);
        }
        return result;
    }

    void SetFromPackedWord(uint32_t offset, uint32_t packed_word)
    {
        DASSERT(offset % 4 == 0);
        for (uint32_t i = 0; i < 4; ++i) {
            data[offset + i] = packed_word >> ((3 - i) * 8);
        }
    }
};

class DisplayPort {
public:
    // This is the I2C address for DDC, for fetching EDID data.
    static constexpr int kDdcI2cAddress = 0x50;

    static bool FetchEdidData(RegisterIo* dev, uint8_t* buf, uint32_t size);
    static void FetchAndCheckEdidData(RegisterIo* reg_io);
};

#endif // MODESET_DISPLAYPORT_H
