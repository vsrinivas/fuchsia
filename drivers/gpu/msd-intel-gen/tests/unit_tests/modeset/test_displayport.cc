// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_mmio.h"
#include "modeset/displayport.h"
#include "register_io.h"
#include "registers.h"
#include "gtest/gtest.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

namespace {

class ExampleEdidData {
public:
    ExampleEdidData()
    {
        // Fill out some dummy EDID data.
        for (uint32_t i = 0; i < sizeof(data); ++i) {
            data[i] = i;
        }
    }

    // The base EDID data is 128 bytes.  TODO(MA-150): Add support for
    // the extended versions, which are larger,
    uint8_t data[128];
};

// This represents an I2C bus on which there is a DDC device, and the DDC
// device can report some EDID data.
class DdcI2cBus {
public:
    bool I2cRead(uint32_t addr, uint8_t* buf, uint32_t size)
    {
        if (addr == DisplayPort::kDdcI2cAddress) {
            for (uint32_t i = 0; i < size; ++i)
                buf[i] = ReadByte();
            return true;
        }
        return false;
    }

    bool I2cWrite(uint32_t addr, const uint8_t* buf, uint32_t size)
    {
        if (addr == DisplayPort::kDdcI2cAddress) {
            // Any byte sent to this address sets the seek position.
            for (uint32_t i = 0; i < size; ++i)
                seek_pos_ = buf[i];
            return true;
        }
        return false;
    }

private:
    ExampleEdidData edid_data_;
    uint32_t seek_pos_ = 0;

    uint8_t ReadByte()
    {
        if (seek_pos_ < sizeof(edid_data_))
            return edid_data_.data[seek_pos_++];
        // If we read past the end of the EDID data, then return zeroes.
        // At least one real display that I tested does that.  (Another
        // possibility would be for the device to NACK the I2C read
        // request.)
        return 0;
    }
};

// This represents a DisplayPort Aux channel.  This implements sending I2C
// messages over the Aux channel.
class DpAux {
    DdcI2cBus i2c_;

public:
    void SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply)
    {
        assert(request->size <= DpAuxMessage::kMaxTotalSize);
        // TODO(MA-150): Allow messages with an empty body, for which
        // request->size == 3 (because the body size field is omitted).
        assert(request->size >= 4);
        uint32_t dp_cmd = request->data[0] >> 4;
        uint32_t addr =
            ((request->data[0] & 0xf) << 16) | (request->data[1] << 8) | request->data[2];
        uint32_t dp_size = request->data[3] + 1;

        if (dp_cmd == 0) {
            // I2C write
            assert(request->size == 4 + dp_size);

            bool ok = i2c_.I2cWrite(addr, &request->data[4], dp_size);
            assert(ok);

            reply->size = 1;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else if (dp_cmd == 1) {
            // I2C read
            // There should be no extra data in the input message.
            assert(request->size == 4);
            // This is the maximum amount we can read in a single I2C-read-over-DP.
            assert(dp_size <= DpAuxMessage::kMaxBodySize);

            bool ok = i2c_.I2cRead(addr, &reply->data[1], dp_size);
            assert(ok);
            reply->size = 1 + dp_size;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else {
            assert(0);
        }
    }
};

uint32_t SetBits(uint32_t reg_value, uint32_t shift, uint32_t mask, uint32_t field_value)
{
    assert((field_value & ~mask) == 0);
    reg_value &= ~(mask << shift); // Clear the existing field value.
    reg_value |= field_value << shift;
    return reg_value;
}

// This represents the MMIO registers of an Intel graphics device.  It
// represents the subset of registers used for sending messages over the
// DisplayPort Aux channel.
class TestDevice : public RegisterIo::Hook {
    DpAux dp_aux_;
    magma::PlatformMmio* mmio_;

public:
    TestDevice(magma::PlatformMmio* mmio) : mmio_(mmio) {}

    void Write32(uint32_t offset, uint32_t value)
    {
        if (offset == registers::DDIAuxControl::kOffset &&
            (value & registers::DDIAuxControl::kSendBusyBit)) {
            uint32_t other_flags = value &
                                   ~(registers::DDIAuxControl::kSendBusyBit |
                                     (registers::DDIAuxControl::kMessageSizeMask
                                      << registers::DDIAuxControl::kMessageSizeShift));
            assert(other_flags == registers::DDIAuxControl::kFlags);

            DpAuxMessage request;
            DpAuxMessage reply;

            // Read the request message from registers.
            request.size = (value >> registers::DDIAuxControl::kMessageSizeShift) &
                           registers::DDIAuxControl::kMessageSizeMask;
            assert(request.size <= DpAuxMessage::kMaxTotalSize);
            for (uint32_t offset = 0; offset < request.size; offset += 4) {
                request.SetFromPackedWord(offset,
                                          mmio_->Read32(registers::DDIAuxData::kOffset + offset));
            }
            dp_aux_.SendDpAuxMsg(&request, &reply);

            // Write the reply message into registers.
            assert(reply.size <= DpAuxMessage::kMaxTotalSize);
            for (uint32_t offset = 0; offset < reply.size; offset += 4) {
                mmio_->Write32(reply.GetPackedWord(offset),
                               registers::DDIAuxData::kOffset + offset);
            }

            // Update the register to mark the transaction as completed.
            // (Note that since we do this immediately, we are not
            // exercising the polling logic in the software-under-test.)
            value &= ~registers::DDIAuxControl::kSendBusyBit;
            value = SetBits(value, registers::DDIAuxControl::kMessageSizeShift,
                            registers::DDIAuxControl::kMessageSizeMask, reply.size);
            mmio_->Write32(value, offset);
        }
    }

    void Read32(uint32_t offset, uint32_t val) {}

    void Read64(uint32_t offset, uint64_t val) {}
};

class TestDisplayPort {
};

// Test encoding and decoding of DP Aux messages to and from the big-endian
// words that the Intel hardware uses.
TEST(DisplayPort, DpAuxWordPacking)
{
    // Test encoding.
    DpAuxMessage msg;
    memcpy(msg.data, "\x11\x22\x33\x44\x55\x66\x77\x88", 8);
    msg.size = 7;
    ASSERT_EQ(msg.GetPackedWord(0), 0x11223344U);
    ASSERT_EQ(msg.GetPackedWord(4), 0x55667700U);

    // Test decoding.
    DpAuxMessage msg2;
    msg2.SetFromPackedWord(0, msg.GetPackedWord(0));
    msg2.SetFromPackedWord(4, msg.GetPackedWord(4));
    ASSERT_EQ(0, memcmp(msg2.data, msg.data, msg.size));
}

void ReadbackTest(RegisterIo* reg_io)
{
    ExampleEdidData expected_data;
    uint8_t buf[sizeof(expected_data.data)];
    ASSERT_TRUE(DisplayPort::FetchEdidData(reg_io, buf, sizeof(buf)));
    ASSERT_EQ(0, memcmp(buf, expected_data.data, sizeof(buf)));
}

TEST(DisplayPort, ReadbackTest)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    reg_io.InstallHook(std::make_unique<TestDevice>(reg_io.mmio()));

    ReadbackTest(&reg_io);
    // Running this test a second time checks that the seek position is reset.
    ReadbackTest(&reg_io);
}

} // namespace
