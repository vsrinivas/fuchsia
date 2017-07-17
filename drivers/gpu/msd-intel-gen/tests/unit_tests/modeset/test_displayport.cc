// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "example_edid.h"
#include "mock/mock_mmio.h"
#include "modeset/displayport.h"
#include "modeset/edid.h"
#include "register_io.h"
#include "registers.h"
#include "registers_ddi.h"
#include "registers_dpll.h"
#include "registers_pipe.h"
#include "registers_transcoder.h"
#include "gtest/gtest.h"
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

bool DdiClockIsConfigured(magma::PlatformMmio* reg_io, uint32_t ddi_number)
{
    // Assumptions: This test currently only knows how to check for DDI C
    // and DPLL 1.
    if (ddi_number != 2)
        return DRETF(false, "Unhandled DDI number");
    uint32_t expected_dpll = 1;

    // Is power enabled for this DDI?
    auto power_reg = registers::PowerWellControl2::Get().ReadFrom(reg_io);
    if (!power_reg.ddi_c_io_power_request().get())
        return DRETF(false, "Power not enabled for DDI");

    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(reg_io);
    if (dpll_ctrl2.ddi_c_clock_select().get() != expected_dpll)
        return false;

    auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(reg_io);
    if (dpll_ctrl1.dpll1_hdmi_mode().get())
        return DRETF(false, "DPLL not in DisplayPort mode");
    if (dpll_ctrl1.dpll1_link_rate().get() != dpll_ctrl1.kLinkRate1350Mhz)
        return DRETF(false, "DPLL set to wrong link rate");
    // Currently we don't care about the fields ssc_enable and override.

    auto lcpll_ctrl = registers::Lcpll2Control::Get().ReadFrom(reg_io);
    if (!lcpll_ctrl.enable_dpll1().get())
        return DRETF(false, "DPLL not enabled");

    return true;
}

bool DdiIsSendingLinkTrainingPattern(magma::PlatformMmio* reg_io, uint32_t ddi_number,
                                     int which_pattern)
{
    registers::DdiRegs ddi(ddi_number);

    auto dp_tp = ddi.DdiDpTransportControl().ReadFrom(reg_io);
    if (!dp_tp.transport_enable().get())
        return DRETF(false, "DDI not enabled");
    if (which_pattern == 1) {
        if (dp_tp.dp_link_training_pattern().get() != dp_tp.kTrainingPattern1)
            return DRETF(false, "Training pattern 1 not set");
    } else {
        DASSERT(which_pattern == 2);
        if (dp_tp.dp_link_training_pattern().get() != dp_tp.kTrainingPattern2)
            return DRETF(false, "Training pattern 2 not set");
    }

    uint32_t dp_lane_count = 2;

    auto buf_ctl = ddi.DdiBufControl().ReadFrom(reg_io);
    if (!buf_ctl.ddi_buffer_enable().get())
        return DRETF(false, "DDI buffer not enabled");
    if (buf_ctl.dp_port_width_selection().get() != dp_lane_count - 1)
        return DRETF(false, "DDI lane count not set correctly");

    return true;
}

// This represents a test instance of a DisplayPort sink device's DPCD
// (DisplayPort Configuration Data).
class Dpcd {
public:
    Dpcd(magma::PlatformMmio* mmio, uint32_t ddi_number) : mmio_(mmio), ddi_number_(ddi_number) {}

    void DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size)
    {
        for (uint32_t i = 0; i < size; ++i)
            buf[i] = map_[addr + i];
    }

    void DpcdWrite(uint32_t addr, const uint8_t* buf, uint32_t size)
    {
        // The spec says that when writing to TRAINING_PATTERN_SET, "The
        // AUX CH burst write must be used for writing to
        // TRAINING_LANEx_SET bytes of the enabled lanes".  (From section
        // 3.5.1.3, "Link Training", in v1.1a.)  Check for that here.
        if (addr == DisplayPort::DPCD_TRAINING_PATTERN_SET && size == 3) {
            HandleLinkTrainingRequest(buf[0]);
        }

        for (uint32_t i = 0; i < size; ++i)
            map_[addr + i] = buf[i];
    }

private:
    void HandleLinkTrainingRequest(uint8_t reg_byte)
    {
        // If the source device's clock is not configured, link training
        // won't succeed.
        if (!DdiClockIsConfigured(mmio_, ddi_number_))
            return;

        // Unpack the register value.
        dpcd::TrainingPatternSet reg;
        reg.set_reg_value(reg_byte);

        if (!reg.scrambling_disable().get())
            return;

        if (reg.training_pattern_set().get() == reg.kTrainingPattern1) {
            if (!DdiIsSendingLinkTrainingPattern(mmio_, ddi_number_, 1))
                return;

            // Indicate that training phase 1 was successful.
            dpcd::Lane01Status lane_status;
            lane_status.lane0_cr_done().set(1);
            lane_status.lane1_cr_done().set(1);
            map_[DisplayPort::DPCD_LANE0_1_STATUS] = lane_status.reg_value();
        } else if (reg.training_pattern_set().get() == reg.kTrainingPattern2) {
            if (!DdiIsSendingLinkTrainingPattern(mmio_, ddi_number_, 2))
                return;

            // Indicate that training phase 2 was successful.
            dpcd::Lane01Status lane_status;
            lane_status.lane0_cr_done().set(1);
            lane_status.lane1_cr_done().set(1);
            lane_status.lane0_channel_eq_done().set(1);
            lane_status.lane1_channel_eq_done().set(1);
            lane_status.lane0_symbol_locked().set(1);
            lane_status.lane1_symbol_locked().set(1);
            map_[DisplayPort::DPCD_LANE0_1_STATUS] = lane_status.reg_value();
        }
    }

    // Info about DisplayPort sink device:
    // Mapping from DPCD register address to register value.
    std::map<uint32_t, uint8_t> map_;

    // Info about DisplayPort source device, for testing.
    magma::PlatformMmio* mmio_;
    uint32_t ddi_number_;
};

// This represents a DisplayPort Aux channel.  This implements sending I2C
// messages over the Aux channel.
class DpAux {
public:
    DpAux(magma::PlatformMmio* mmio, uint32_t ddi_number) : dpcd_(mmio, ddi_number) {}

    // On return, |*timeout_result| indicates whether the response to this
    // request is a timeout.
    void SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply, bool* timeout_result)
    {
        reply->size = 0;
        *timeout_result = false;

        ASSERT_LE(request->size, uint32_t{DpAuxMessage::kMaxTotalSize});
        // TODO(MA-150): Allow messages with an empty body, for which
        // request->size == 3 (because the body size field is omitted).
        ASSERT_GE(request->size, 4u);
        uint32_t dp_cmd = request->data[0] >> 4;
        uint32_t addr =
            ((request->data[0] & 0xf) << 16) | (request->data[1] << 8) | request->data[2];
        uint32_t dp_size = request->data[3] + 1;

        if (ShouldSendTimeout()) {
            *timeout_result = true;
            return;
        }
        if (ShouldSendDefer()) {
            // Send an AUX_DEFER reply to exercise handling of them.
            reply->size = 1;
            reply->data[0] = DisplayPort::DP_REPLY_AUX_DEFER << 4;
            return;
        }

        if (dp_cmd == DisplayPort::DP_REQUEST_I2C_WRITE ||
            dp_cmd == DisplayPort::DP_REQUEST_NATIVE_WRITE) {
            ASSERT_EQ(request->size, 4 + dp_size);

            if (dp_cmd == DisplayPort::DP_REQUEST_I2C_WRITE) {
                ASSERT_TRUE(i2c_.I2cWrite(addr, &request->data[4], dp_size));
            } else {
                dpcd_.DpcdWrite(addr, &request->data[4], dp_size);
            }

            reply->size = 1;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else if (dp_cmd == DisplayPort::DP_REQUEST_I2C_READ ||
                   dp_cmd == DisplayPort::DP_REQUEST_NATIVE_READ) {
            // There should be no extra data in the input message.
            ASSERT_EQ(request->size, 4u);
            // This is the maximum amount we can read in a single I2C-read-over-DP.
            ASSERT_LE(dp_size, uint32_t{DpAuxMessage::kMaxBodySize});

            if (dp_cmd == DisplayPort::DP_REQUEST_I2C_READ) {
                ASSERT_TRUE(i2c_.I2cRead(addr, &reply->data[1], dp_size));
            } else {
                dpcd_.DpcdRead(addr, &reply->data[1], dp_size);
            }

            reply->size = 1 + dp_size;
            reply->data[0] = 0; // Header byte: indicates an ack
        } else {
            FAIL() << "Unhandled DisplayPort Aux message type: " << dp_cmd;
        }
    }

    ExampleEdidData* get_edid_data() { return i2c_.get_edid_data(); }

private:
    // Number of AUX DEFER replies we should send before we send a real
    // non-defer reply.
    static constexpr unsigned kDefersToSend = 7;

    bool ShouldSendTimeout()
    {
        // Generate one timeout in response to a request before giving
        // non-timeout replies.  This mimics one DisplayPort monitor that
        // I've tested on.
        if (timeout_sent_)
            return false;
        timeout_sent_ = true;
        return true;
    }

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
    Dpcd dpcd_;
    bool timeout_sent_ = false;
    // Number of AUX DEFER replies sent since the last non-defer reply (or
    // since the start).
    unsigned defer_count_ = 0;
};

// This represents the MMIO registers of an Intel graphics device.  It
// represents the subset of registers used for sending messages over the
// DisplayPort Aux channel.
class TestDevice : public RegisterIo::Hook {
public:
    TestDevice(magma::PlatformMmio* mmio) : mmio_(mmio)
    {
        for (uint32_t ddi_number = 0; ddi_number < registers::DdiRegs::kDdiCount; ++ddi_number) {
            dp_aux_[ddi_number].reset(new DpAux(mmio, ddi_number));
        }
    }

    void WriteDdiAuxControl(uint32_t ddi_number, uint32_t value)
    {
        registers::DdiRegs ddi(ddi_number);
        auto control = ddi.DdiAuxControl().FromValue(value);

        // This mimics what the hardware does.  Counterintuitively, writing
        // 1 to this timeout bit tells the hardware to reset this bit to 0.
        // If we write 0 into the timeout bit, the hardware ignores that
        // and leaves the bit's value unchanged.
        if (control.timeout().get()) {
            control.timeout().set(0);
        } else {
            // Restore the previous value of the timeout bit (from before
            // the register write that we are handling).  Note that this is
            // necessary because the RegisterIo class's hook facility
            // currently doesn't allow us to intercept this write before it
            // is applied to the PlatformMmio object (mmio_).
            control.timeout().set(prev_timeout_bit_[ddi_number]);
        }

        if (control.send_busy().get()) {
            ASSERT_EQ(control.sync_pulse_count().get(), 31U);

            DpAuxMessage request;
            DpAuxMessage reply;

            uint32_t data_reg = ddi.DdiAuxData().addr();

            // Read the request message from registers.
            request.size = control.message_size().get();
            ASSERT_LE(request.size, uint32_t{DpAuxMessage::kMaxTotalSize});
            for (uint32_t offset = 0; offset < request.size; offset += 4) {
                request.SetFromPackedWord(offset, mmio_->Read32(data_reg + offset));
            }
            bool got_timeout = false;
            dp_aux_[ddi_number]->SendDpAuxMsg(&request, &reply, &got_timeout);

            control.timeout().set(got_timeout);

            if (!got_timeout) {
                // Write the reply message into registers.
                ASSERT_LE(reply.size, uint32_t{DpAuxMessage::kMaxTotalSize});
                for (uint32_t offset = 0; offset < reply.size; offset += 4) {
                    mmio_->Write32(data_reg + offset, reply.GetPackedWord(offset));
                }
                control.message_size().set(reply.size);
            }

            // Update the register to mark the transaction as completed.
            // (Note that since we do this immediately, we are not
            // exercising the polling logic in the software-under-test.)
            control.send_busy().set(0);
        }

        control.WriteTo(mmio_);
        // Save the timeout bit for next time.
        prev_timeout_bit_[ddi_number] = control.timeout().get();
    }

    void Write32(uint32_t offset, uint32_t value)
    {
        for (uint32_t ddi_number = 0; ddi_number < registers::DdiRegs::kDdiCount; ++ddi_number) {
            registers::DdiRegs ddi(ddi_number);
            if (offset == ddi.DdiAuxControl().addr()) {
                WriteDdiAuxControl(ddi_number, value);
            }
        }
    }

    void Read32(uint32_t offset, uint32_t val) {}

    void Read64(uint32_t offset, uint64_t val) {}

    ExampleEdidData* get_edid_data(uint32_t ddi_number)
    {
        return dp_aux_[ddi_number]->get_edid_data();
    }

private:
    std::unique_ptr<DpAux> dp_aux_[registers::DdiRegs::kDdiCount];
    bool prev_timeout_bit_[registers::DdiRegs::kDdiCount] = {};
    magma::PlatformMmio* mmio_;
};

TEST(DisplayPort, BitfieldHandling)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));

    uint32_t ddi_number = 2;
    registers::DdiRegs ddi(ddi_number);

    uint32_t addr = 0x64010 + 0x100 * ddi_number;
    EXPECT_EQ(reg_io.Read32(addr), 0U);
    reg_io.Write32(addr, 0x100089);

    // Using ReadFrom() should preserve the value 0x89 in the lower bits.
    auto reg1 = ddi.DdiAuxControl().ReadFrom(&reg_io);
    reg1.message_size().set(6);
    reg1.WriteTo(&reg_io);
    EXPECT_EQ(reg_io.Read32(addr), 0x600089U);

    // The following will ignore the existing value and zero out the value
    // in the lower bits.
    auto reg2 = ddi.DdiAuxControl().FromValue(0);
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

// Test reading and writing a DisplayPort sink device's DPCD.
TEST(DisplayPort, DpcdReadAndWrite)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    reg_io.InstallHook(std::make_unique<TestDevice>(reg_io.mmio()));

    DpAuxChannel dp_aux(&reg_io, 0);

    // Test that we handle 20-bit addresses.
    uint32_t addr1 = 0x54321;
    uint32_t addr2 = 0x4321;

    // Write some data.
    uint8_t write_data1[] = {0x44, 0x55};
    uint8_t write_data2[] = {0x66};
    EXPECT_TRUE(dp_aux.DpcdWrite(addr1, write_data1, sizeof(write_data1)));
    EXPECT_TRUE(dp_aux.DpcdWrite(addr2, write_data2, sizeof(write_data2)));

    // Check that we can read back the same data.
    uint8_t read_data1[2] = {};
    uint8_t read_data2[1] = {};
    EXPECT_TRUE(dp_aux.DpcdRead(addr1, read_data1, sizeof(read_data1)));
    EXPECT_TRUE(dp_aux.DpcdRead(addr2, read_data2, sizeof(read_data2)));
    EXPECT_EQ(read_data1[0], 0x44);
    EXPECT_EQ(read_data1[1], 0x55);
    EXPECT_EQ(read_data2[0], 0x66);
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

TEST(DisplayPort, LinkTraining)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    TestDevice* test_device = new TestDevice(reg_io.mmio());
    reg_io.InstallHook(std::unique_ptr<TestDevice>(test_device));

    BaseEdid edid = GetExampleEdid();

    uint32_t ddi_number = 2;
    EXPECT_TRUE(DisplayPort::PartiallyBringUpDisplay(&reg_io, ddi_number, &edid));

    // Check that the training code leaves TRAINING_PATTERN_SET set to
    // 0, to end the sink device's training mode.
    DpAuxChannel dp_aux(&reg_io, ddi_number);
    uint8_t reg_byte;
    EXPECT_TRUE(
        dp_aux.DpcdRead(DisplayPort::DPCD_TRAINING_PATTERN_SET, &reg_byte, sizeof(reg_byte)));
    EXPECT_EQ(reg_byte, 0);
}

static void CheckRegs(RegisterIo* reg_io)
{
    uint32_t pipe_number = 1;  // Pipe B
    uint32_t trans_number = 1; // Transcoder B
    uint32_t ddi_number = 2;   // DDI C
    registers::PipeRegs pipe(pipe_number);
    registers::TranscoderRegs trans(trans_number);

    // DisplayPort clock ratios.
    auto data_m = trans.DataM().ReadFrom(reg_io);
    auto data_n = trans.DataN().ReadFrom(reg_io);
    auto link_m = trans.LinkM().ReadFrom(reg_io);
    auto link_n = trans.LinkN().ReadFrom(reg_io);

    EXPECT_EQ(data_m.tu_or_vcpayload_size().get(), 63u);
    EXPECT_EQ(data_m.data_m_value().get(), 0x522222u);
    EXPECT_EQ(data_n.data_n_value().get(), 0x800000u);
    EXPECT_EQ(link_m.link_m_value().get(), 0x4901e5u);
    EXPECT_EQ(link_n.link_n_value().get(), 0x800000u);

    // CRT timing parameters.
    auto h_total = trans.HTotal().ReadFrom(reg_io);
    auto h_blank = trans.HBlank().ReadFrom(reg_io);
    auto h_sync = trans.HSync().ReadFrom(reg_io);

    auto v_total = trans.VTotal().ReadFrom(reg_io);
    auto v_blank = trans.VBlank().ReadFrom(reg_io);
    auto v_sync = trans.VSync().ReadFrom(reg_io);

    EXPECT_EQ(h_total.count_active().get(), 1919u);
    EXPECT_EQ(h_total.count_total().get(), 2079u);
    EXPECT_EQ(h_blank.reg_value(), h_total.reg_value());
    EXPECT_EQ(h_sync.sync_start().get(), 1967u);
    EXPECT_EQ(h_sync.sync_end().get(), 1999u);

    EXPECT_EQ(v_total.count_active().get(), 1199u);
    EXPECT_EQ(v_total.count_total().get(), 1234u);
    EXPECT_EQ(v_blank.reg_value(), v_total.reg_value());
    EXPECT_EQ(v_sync.sync_start().get(), 1202u);
    EXPECT_EQ(v_sync.sync_end().get(), 1208u);

    // Pipe config.
    auto pipe_size = pipe.PipeSourceSize().ReadFrom(reg_io);
    EXPECT_EQ(pipe_size.horizontal_source_size().get(), 1919u);
    EXPECT_EQ(pipe_size.vertical_source_size().get(), 1199u);

    // Transcoder config.
    auto clock_select = trans.ClockSelect().ReadFrom(reg_io);
    EXPECT_EQ(clock_select.trans_clock_select().get(), ddi_number + 1);

    auto msa_misc = trans.MsaMisc().ReadFrom(reg_io);
    EXPECT_EQ(msa_misc.sync_clock().get(), 1u);

    auto ddi_func = trans.DdiFuncControl().ReadFrom(reg_io);
    EXPECT_EQ(ddi_func.trans_ddi_function_enable().get(), 1u);
    EXPECT_EQ(ddi_func.ddi_select().get(), ddi_number);
    EXPECT_EQ(ddi_func.trans_ddi_mode_select().get(), uint32_t{ddi_func.kModeDisplayPortSst});
    EXPECT_EQ(ddi_func.bits_per_color().get(), 2u);
    EXPECT_EQ(ddi_func.sync_polarity().get(), 1u);
    EXPECT_EQ(ddi_func.dp_port_width_selection().get(), 1u);

    // These values should get generalized when the software-under-test
    // allocates plane buffer ranges rather than just using a fixed range.
    auto buf_cfg = pipe.PlaneBufCfg().ReadFrom(reg_io);
    EXPECT_EQ(buf_cfg.buffer_start().get(), 0x1beu);
    EXPECT_EQ(buf_cfg.buffer_end().get(), 0x373u);

    auto trans_conf = trans.Conf().ReadFrom(reg_io);
    EXPECT_EQ(trans_conf.transcoder_enable().get(), 1u);

    // Plane config.
    auto plane_control = pipe.PlaneControl().ReadFrom(reg_io);
    EXPECT_EQ(plane_control.plane_enable().get(), 1u);
    EXPECT_EQ(plane_control.pipe_gamma_enable().get(), 1u);
    EXPECT_EQ(plane_control.source_pixel_format().get(), uint32_t{plane_control.kFormatRgb8888});
    EXPECT_EQ(plane_control.plane_gamma_disable().get(), 1u);

    auto plane_size = pipe.PlaneSurfaceSize().ReadFrom(reg_io);
    EXPECT_EQ(plane_size.width_minus_1().get(), 1919u);
    EXPECT_EQ(plane_size.height_minus_1().get(), 1199u);

    // Test for the hard-coded value for now.  Later the code will plumb
    // through the framebuffer's stride.
    auto plane_stride = pipe.PlaneSurfaceStride().ReadFrom(reg_io);
    EXPECT_EQ(plane_stride.stride().get(), 0x87u);

    // Test for the hard-coded value for now.
    auto plane_addr = pipe.PlaneSurfaceAddress().ReadFrom(reg_io);
    EXPECT_EQ(plane_addr.surface_base_address().get(), 0u);
}

TEST(DisplayPort, DisplayBringup)
{
    RegisterIo reg_io(MockMmio::Create(0x100000));
    TestDevice* test_device = new TestDevice(reg_io.mmio());
    reg_io.InstallHook(std::unique_ptr<TestDevice>(test_device));

    BaseEdid edid = GetExampleEdid();

    uint32_t ddi_number = 2;
    EXPECT_TRUE(DisplayPort::PartiallyBringUpDisplay(&reg_io, ddi_number, &edid));

    CheckRegs(&reg_io);
}

} // namespace
