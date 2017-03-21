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

// This class implements sending and receiving messages over the
// DisplayPort Aux channel.  The Aux channel supports read and write
// requests for I2C messages and DisplayPort "native" messages.
class DpAuxChannel {
public:
    DpAuxChannel(RegisterIo* reg_io, uint32_t ddi_number) : reg_io_(reg_io), ddi_number_(ddi_number)
    {
    }

    // Send an I2C read request.  If this fails to read the full
    // |size| bytes into |buf|, it returns false for failure.
    bool I2cRead(uint32_t addr, uint8_t* buf, uint32_t size);
    // Send an I2C write request.
    bool I2cWrite(uint32_t addr, const uint8_t* buf, uint32_t size);

    // Send a "native" read request, reading a range of DPCD bytes starting
    // at |addr|.
    bool DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size);
    // Send a "native" write request, writing to a range of DPCD bytes
    // starting at |addr|.
    bool DpcdWrite(uint32_t addr, const uint8_t* buf, uint32_t size);

private:
    // Send a DisplayPort Aux message and receive the synchronous reply
    // message.
    bool SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply);
    // This is like SendDpAuxMsg(), but it also checks the header field in
    // the reply for whether the request was successful, and it retries the
    // request if the sink device returns an AUX_DEFER reply.
    bool SendDpAuxMsgWithRetry(const DpAuxMessage* request, DpAuxMessage* reply);

    bool DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size);
    // Read a single chunk, upto the DisplayPort Aux message size limit.
    bool DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                        uint32_t* size_out);
    bool DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, uint32_t size);

    RegisterIo* reg_io_;
    uint32_t ddi_number_;
};

class DisplayPort {
public:
    // This is the I2C address for DDC, for fetching EDID data.
    static constexpr int kDdcI2cAddress = 0x50;

    // 4-bit request type in Aux channel request messages.
    enum {
        DP_REQUEST_I2C_WRITE = 0,
        DP_REQUEST_I2C_READ = 1,
        DP_REQUEST_NATIVE_WRITE = 8,
        DP_REQUEST_NATIVE_READ = 9,
    };

    // 4-bit statuses in Aux channel reply messages.
    enum {
        DP_REPLY_AUX_ACK = 0,
        DP_REPLY_AUX_NACK = 1,
        DP_REPLY_AUX_DEFER = 2,
        DP_REPLY_I2C_NACK = 4,
        DP_REPLY_I2C_DEFER = 8,
    };

    static bool FetchEdidData(RegisterIo* dev, uint32_t ddi_number, uint8_t* buf, uint32_t size);
    static void FetchAndCheckEdidData(RegisterIo* reg_io);
};

#endif // MODESET_DISPLAYPORT_H
