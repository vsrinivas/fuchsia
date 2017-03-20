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

struct ExampleEdidData {
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

    ExampleEdidData* get_edid_data() { return &edid_data_; }

private:
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

    ExampleEdidData edid_data_;
    uint32_t seek_pos_ = 0;
};

// This represents a DisplayPort Aux channel.  This implements sending I2C
// messages over the Aux channel.
class DpAux {
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

        if (ShouldSendDefer()) {
            // Send an AUX_DEFER reply to exercise handling of them.
            reply->size = 1;
            reply->data[0] = DisplayPort::DP_REPLY_AUX_DEFER << 4;
            return;
        }

        if (dp_cmd == 0) {
            // I2C write
            assert(request->size == 4 + dp_size);

            ASSERT_TRUE(i2c_.I2cWrite(addr, &request->data[4], dp_size));

            reply->size = 1;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else if (dp_cmd == 1) {
            // I2C read
            // There should be no extra data in the input message.
            assert(request->size == 4);
            // This is the maximum amount we can read in a single I2C-read-over-DP.
            assert(dp_size <= DpAuxMessage::kMaxBodySize);

            ASSERT_TRUE(i2c_.I2cRead(addr, &reply->data[1], dp_size));
            reply->size = 1 + dp_size;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else {
            assert(0);
        }
    }

    ExampleEdidData* get_edid_data() { return i2c_.get_edid_data(); }

private:
    // Number of AUX DEFER replies we should send before we send a real
    // non-defer reply.
    static constexpr unsigned kDefersToSend = 7;

    bool ShouldSendDefer()
    {
        if (defer_count_ == kDefersToSend) {
            defer_count_ = 0;
            return false;
        }
        ++defer_count_;
        return true;
    }

    DdcI2cBus i2c_;
    // Number of AUX DEFER replies sent since the last non-defer reply (or
    // since the start).
    unsigned defer_count_ = 0;
};

// This represents the MMIO registers of an Intel graphics device.  It
// represents the subset of registers used for sending messages over the
// DisplayPort Aux channel.
class TestDevice : public RegisterIo::Hook {
public:
    TestDevice(magma::PlatformMmio* mmio) : mmio_(mmio) {}

    void WriteDdiAuxControl(uint32_t ddi_number, uint32_t value)
    {
        auto control = registers::DdiAuxControl::Get(ddi_number).FromValue(value);

        if (control.send_busy().get()) {
            ASSERT_EQ(control.sync_pulse_count().get(), 31U);

            DpAuxMessage request;
            DpAuxMessage reply;

            uint32_t data_reg = registers::DdiAuxData::GetOffset(ddi_number);

            // Read the request message from registers.
            request.size = control.message_size().get();
            assert(request.size <= DpAuxMessage::kMaxTotalSize);
            for (uint32_t offset = 0; offset < request.size; offset += 4) {
                request.SetFromPackedWord(offset, mmio_->Read32(data_reg + offset));
            }
            dp_aux_[ddi_number].SendDpAuxMsg(&request, &reply);

            // Write the reply message into registers.
            assert(reply.size <= DpAuxMessage::kMaxTotalSize);
            for (uint32_t offset = 0; offset < reply.size; offset += 4) {
                mmio_->Write32(data_reg + offset, reply.GetPackedWord(offset));
            }

            // Update the register to mark the transaction as completed.
            // (Note that since we do this immediately, we are not
            // exercising the polling logic in the software-under-test.)
            control.send_busy().set(0);
            control.message_size().set(reply.size);
            mmio_->Write32(control.reg_addr(), control.reg_value());
        }
    }

    void Write32(uint32_t offset, uint32_t value)
    {
        for (uint32_t ddi_number = 0; ddi_number < registers::Ddi::kDdiCount; ++ddi_number) {
            if (offset == registers::DdiAuxControl::Get(ddi_number).addr()) {
                WriteDdiAuxControl(ddi_number, value);
            }
        }
    }

    void Read32(uint32_t offset, uint32_t val) {}

    void Read64(uint32_t offset, uint64_t val) {}

    ExampleEdidData* get_edid_data(uint32_t ddi_number)
    {
        return dp_aux_[ddi_number].get_edid_data();
    }

private:
    DpAux dp_aux_[registers::Ddi::kDdiCount];
    magma::PlatformMmio* mmio_;
};

class TestDisplayPort {
};

TEST(DisplayPort, BitfieldHandling)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));

    uint32_t ddi_number = 2;
    uint32_t addr = 0x64010 + 0x100 * ddi_number;
    EXPECT_EQ(reg_io.Read32(addr), 0U);
    reg_io.Write32(addr, 0x100089);

    // Using ReadFrom() should preserve the value 0x89 in the lower bits.
    auto reg1 = registers::DdiAuxControl::Get(ddi_number).ReadFrom(&reg_io);
    reg1.message_size().set(6);
    reg1.WriteTo(&reg_io);
    EXPECT_EQ(reg_io.Read32(addr), 0x600089U);

    // The following will ignore the existing value and zero out the value
    // in the lower bits.
    auto reg2 = registers::DdiAuxControl::Get(ddi_number).FromValue(0);
    reg2.message_size().set(5);
    reg2.WriteTo(&reg_io);
    EXPECT_EQ(reg_io.Read32(addr), 0x500000U);
}

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

void ReadbackTest(RegisterIo* reg_io, uint32_t ddi_number, ExampleEdidData* expected_data)
{
    uint8_t buf[sizeof(expected_data->data)];
    ASSERT_TRUE(DisplayPort::FetchEdidData(reg_io, ddi_number, buf, sizeof(buf)));
    ASSERT_EQ(0, memcmp(buf, expected_data->data, sizeof(buf)));
}

TEST(DisplayPort, ReadbackTest)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    reg_io.InstallHook(std::make_unique<TestDevice>(reg_io.mmio()));

    ExampleEdidData expected_data;
    ReadbackTest(&reg_io, 0, &expected_data);
    // Running this test a second time checks that the seek position is reset.
    ReadbackTest(&reg_io, 0, &expected_data);
}

TEST(DisplayPort, ReadbackTestMultipleDdis)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    TestDevice* test_device = new TestDevice(reg_io.mmio());
    reg_io.InstallHook(std::unique_ptr<TestDevice>(test_device));

    // Make the EDID data different for the two DDIs.
    test_device->get_edid_data(0)->data[6] = 0x88;
    test_device->get_edid_data(1)->data[6] = 0x99;

    ReadbackTest(&reg_io, 0, test_device->get_edid_data(0));
    ReadbackTest(&reg_io, 1, test_device->get_edid_data(1));
}

} // namespace
