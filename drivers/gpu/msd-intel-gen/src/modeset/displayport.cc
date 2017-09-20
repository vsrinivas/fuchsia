// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modeset/displayport.h"
#include "magma_util/macros.h"
#include "modeset/edid.h"
#include "register_io.h"
#include "registers.h"
#include "registers_ddi.h"
#include "registers_dpll.h"
#include "registers_pipe.h"
#include "registers_transcoder.h"
#include <algorithm>
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <thread>

namespace {

// Fill out the header of a DisplayPort Aux message.  For write operations,
// |body_size| is the size of the body of the message to send.  For read
// operations, |body_size| is the size of our receive buffer.
bool SetDpAuxHeader(DpAuxMessage* msg, uint32_t addr, uint32_t dp_cmd, uint32_t body_size)
{
    if (body_size > DpAuxMessage::kMaxBodySize)
        return DRETF(false, "DP aux: Message too large");
    // For now, we don't handle messages with empty bodies.  (However, they
    // can be used for checking whether there is an I2C device at a given
    // address.)
    if (body_size == 0)
        return DRETF(false, "DP aux: Empty message not supported");
    // Addresses should fit into 20 bits.
    if (addr >= (1 << 20))
        return DRETF(false, "DP aux: Address is too large: 0x%x", addr);
    msg->data[0] = (dp_cmd << 4) | ((addr >> 16) & 0xf);
    msg->data[1] = addr >> 8;
    msg->data[2] = addr;
    // For writes, the size of the message will be encoded twice:
    //  * The msg->size field contains the total message size (header and
    //    body).
    //  * If the body of the message is non-empty, the header contains an
    //    extra field specifying the body size (in bytes minus 1).
    // For reads, the message to send is a header only.
    msg->size = 4;
    msg->data[3] = body_size - 1;
    return true;
}

} // namespace

bool DpAuxChannel::SendDpAuxMsg(const DpAuxMessage* request, DpAuxMessage* reply,
                                bool* timeout_result)
{
    *timeout_result = false;

    registers::DdiRegs ddi(ddi_number_);
    uint32_t data_reg = ddi.DdiAuxData().addr();

    // Write the outgoing message to the hardware.
    DASSERT(request->size <= DpAuxMessage::kMaxTotalSize);
    for (uint32_t offset = 0; offset < request->size; offset += 4) {
        reg_io_->Write32(data_reg + offset, request->GetPackedWord(offset));
    }

    auto status = ddi.DdiAuxControl().FromValue(0);
    status.sync_pulse_count().set(31);
    status.message_size().set(request->size);
    // Counterintuitively, writing 1 to this timeout bit tells the hardware
    // to reset the bit's value to 0.  (If we write 0 into the timeout bit,
    // the hardware ignores that and leaves the bit's value unchanged.)
    status.timeout().set(1);
    // Setting the send_busy bit initiates the transaction.
    status.send_busy().set(1);
    status.WriteTo(reg_io_);

    // Poll for the reply message.
    const int kNumTries = 10000;
    for (int tries = 0; tries < kNumTries; ++tries) {
        auto status = ddi.DdiAuxControl().ReadFrom(reg_io_);
        if (!status.send_busy().get()) {
            if (status.timeout().get()) {
                *timeout_result = true;
                return false;
            }
            reply->size = status.message_size().get();
            if (reply->size > DpAuxMessage::kMaxTotalSize)
                return DRETF(false, "DP aux: Invalid reply size");
            // Read the reply message from the hardware.
            for (uint32_t offset = 0; offset < reply->size; offset += 4) {
                reply->SetFromPackedWord(offset, reg_io_->Read32(data_reg + offset));
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    return DRETF(false, "DP aux: No reply after %d tries", kNumTries);
}

bool DpAuxChannel::SendDpAuxMsgWithRetry(const DpAuxMessage* request, DpAuxMessage* reply)
{
    // If the DisplayPort sink device isn't ready to handle an Aux message,
    // it can return an AUX_DEFER reply, which means we should retry the
    // request.
    //
    // The DisplayPort spec does not specify exactly how many retries we
    // should do or how long we should retry for, except to say that we
    // should do at least 7 retries, but that we might need to do a lot
    // more retries.
    //
    // The spec says "A DP Source device is required to retry at least
    // seven times upon receiving AUX_DEFER before giving up the AUX
    // transaction", from section 2.7.7.1.5.6.1 in v1.3.  (AUX_DEFER
    // replies were in earlier versions, but v1.3 clarified the number of
    // retries required.)
    //
    // We will probably need to increase the following number as we find
    // slower displays or adaptors that require more retries.
    const int kMaxDefers = 16;

    // Some DisplayPort sink devices time out on the first DP aux request
    // but succeed on later requests, so we need to retry for some timeouts
    // at least.
    const int kMaxTimeouts = 2;

    unsigned defers_seen = 0;
    unsigned timeouts_seen = 0;

    for (;;) {
        bool timeout_result;
        if (!SendDpAuxMsg(request, reply, &timeout_result)) {
            if (timeout_result) {
                if (++timeouts_seen == kMaxTimeouts)
                    return DRETF(false, "DP aux: Got too many timeouts (%d)", kMaxTimeouts);
                // Retry on timeout.
                continue;
            }
            // We do not retry if sending the raw message failed for
            // an unexpected reason.
            return false;
        }

        // Read the header byte.  This contains a 4-bit status field and 4
        // bits of zero padding.  The status field is in the upper bits
        // because it is sent across the wire first and because DP Aux uses
        // big endian bit ordering.
        if (reply->size < 1)
            return DRETF(false, "DP aux: Unexpected zero-size reply (header byte missing)");
        uint8_t header_byte = reply->data[0];
        uint8_t padding = header_byte & 0xf;
        uint8_t status = header_byte >> 4;
        // Sanity check: The padding should be zero.  If it's not, we
        // shouldn't return an error, in case this space gets used for some
        // later extension to the protocol.  But report it, in case this
        // indicates some problem.
        if (padding)
            magma::log(magma::LOG_WARNING,
                       "DP aux: Reply header padding is non-zero (header byte: 0x%x)", header_byte);

        switch (status) {
            case DisplayPort::DP_REPLY_AUX_ACK:
                // The AUX_ACK implies that we got an I2C ACK too.
                return true;
            case DisplayPort::DP_REPLY_AUX_DEFER:
                if (++defers_seen == kMaxDefers)
                    return DRETF(false, "DP aux: Received too many AUX DEFERs (%d)", kMaxDefers);
                // Go around the loop again to retry.
                continue;
            case DisplayPort::DP_REPLY_AUX_NACK:
                return DRETF(false, "DP aux: Reply was not an ack (got AUX_NACK)");
            case DisplayPort::DP_REPLY_I2C_NACK:
                return DRETF(false, "DP aux: Reply was not an ack (got I2C_NACK)");
            case DisplayPort::DP_REPLY_I2C_DEFER:
                // TODO(MA-150): Implement handling of I2C_DEFER.
                return DRETF(false, "DP aux: Received I2C_DEFER (not implemented)");
            default:
                // We got a reply that is not defined by the DisplayPort spec.
                return DRETF(false, "DP aux: Unrecognized reply (header byte: 0x%x)", header_byte);
        }
    }
}

bool DpAuxChannel::I2cRead(uint32_t addr, uint8_t* buf, uint32_t size)
{
    return DpAuxRead(DisplayPort::DP_REQUEST_I2C_READ, addr, buf, size);
}

bool DpAuxChannel::DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size)
{
    return DpAuxRead(DisplayPort::DP_REQUEST_NATIVE_READ, addr, buf, size);
}

bool DpAuxChannel::I2cWrite(uint32_t addr, const uint8_t* buf, uint32_t size)
{
    return DpAuxWrite(DisplayPort::DP_REQUEST_I2C_WRITE, addr, buf, size);
}

bool DpAuxChannel::DpcdWrite(uint32_t addr, const uint8_t* buf, uint32_t size)
{
    return DpAuxWrite(DisplayPort::DP_REQUEST_NATIVE_WRITE, addr, buf, size);
}

bool DpAuxChannel::DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size)
{
    while (size > 0) {
        uint32_t chunk_size = std::min(size, uint32_t{DpAuxMessage::kMaxBodySize});
        uint32_t bytes_read = 0;
        if (!DpAuxReadChunk(dp_cmd, addr, buf, chunk_size, &bytes_read))
            return false;
        if (bytes_read == 0) {
            // We failed to make progress on the last call.  To avoid the
            // risk of getting an infinite loop from that happening
            // continually, we return.
            return false;
        }
        DASSERT(size >= bytes_read);
        buf += bytes_read;
        size -= bytes_read;
    }
    return true;
}

bool DpAuxChannel::DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                                  uint32_t* size_out)
{
    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!SetDpAuxHeader(&msg, addr, dp_cmd, size_in) || !SendDpAuxMsgWithRetry(&msg, &reply)) {
        return false;
    }
    uint32_t bytes_read = reply.size - 1;
    if (bytes_read > size_in)
        return DRETF(false, "DP aux read: Reply was larger than requested");
    DASSERT(bytes_read <= DpAuxMessage::kMaxBodySize);
    memcpy(buf, &reply.data[1], bytes_read);
    *size_out = bytes_read;
    return true;
}

// This does not support writes more than the message body size limit for
// DisplayPort Aux (16 bytes), since we haven't needed that yet.
bool DpAuxChannel::DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, uint32_t size)
{
    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!SetDpAuxHeader(&msg, addr, dp_cmd, size))
        return false;
    memcpy(&msg.data[4], buf, size);
    msg.size = size + 4;
    if (!SendDpAuxMsgWithRetry(&msg, &reply))
        return false;
    // TODO(MA-150): Handle the case where the hardware did a short write,
    // for which we could send the remaining bytes.
    if (reply.size != 1)
        return DRETF(false, "DP aux write: Unexpected reply size");
    return true;
}

bool DisplayPort::FetchEdidData(RegisterIo* reg_io, uint32_t ddi_number, uint8_t* buf,
                                uint32_t size)
{
    DpAuxChannel i2c(reg_io, ddi_number);

    // Seek to the start of the EDID data, in case the current seek
    // position is non-zero.
    uint8_t data_offset = 0;
    if (!i2c.I2cWrite(kDdcI2cAddress, &data_offset, 1))
        return false;

    // Read the data.
    return i2c.I2cRead(kDdcI2cAddress, buf, size);
}

namespace {

// Tell the sink device to start link training.
bool DpcdRequestLinkTraining(DpAuxChannel* dp_aux, dpcd::TrainingPatternSet* tp_set)
{
    // Set 3 registers with a single write operation.
    //
    // The DisplayPort spec says that we are supposed to write these
    // registers with a single operation: "The AUX CH burst write must be
    // used for writing to TRAINING_LANEx_SET bytes of the enabled lanes."
    // (From section 3.5.1.3, "Link Training", in v1.1a.)
    uint8_t reg_bytes[3];
    reg_bytes[0] = tp_set->reg_value();
    reg_bytes[1] = 0;
    reg_bytes[2] = 0;
    constexpr int kAddr = DisplayPort::DPCD_TRAINING_PATTERN_SET;
    static_assert(kAddr + 1 == DisplayPort::DPCD_TRAINING_LANE0_SET, "");
    static_assert(kAddr + 2 == DisplayPort::DPCD_TRAINING_LANE1_SET, "");

    if (!dp_aux->DpcdWrite(kAddr, reg_bytes, sizeof(reg_bytes)))
        return DRETF(false, "Failure setting TRAINING_PATTERN_SET");
    return true;
}

// Query the sink device for the results of link training.
bool DpcdReadLaneStatus(DpAuxChannel* dp_aux, dpcd::Lane01Status* status)
{
    uint32_t addr = DisplayPort::DPCD_LANE0_1_STATUS;
    uint8_t reg_byte;
    if (!dp_aux->DpcdRead(addr, &reg_byte, sizeof(reg_byte)))
        return DRETF(false, "Failure reading LANE0_1_STATUS");
    status->set_reg_value(reg_byte);
    return true;
}

// This function implements the link training process.  See the "Link
// Training" section in the DisplayPort spec (section 3.5.1.3 in version
// 1.1a).  There are two stages to this:
//  1) Clock Recovery (CR), using training pattern 1.
//  2) Channel Equalization / Symbol-Lock / Inter-lane Alignment, using
//     training pattern 2.
bool LinkTrainingBody(RegisterIo* reg_io, int32_t ddi_number)
{
    DpAuxChannel dp_aux(reg_io, ddi_number);
    registers::DdiRegs ddi(ddi_number);

    // For now, we only support 2 DisplayPort lanes.
    // TODO(MA-150): We should also handle using 1 or 4 lanes.
    uint32_t dp_lane_count = 2;

    auto buf_ctl = ddi.DdiBufControl().FromValue(0);
    buf_ctl.ddi_buffer_enable().set(1);
    buf_ctl.dp_port_width_selection().set(dp_lane_count - 1);
    buf_ctl.WriteTo(reg_io);

    // Link training stage 1.

    // Tell the source device to emit the training pattern.
    auto dp_tp = ddi.DdiDpTransportControl().FromValue(0);
    dp_tp.transport_enable().set(1);
    dp_tp.enhanced_framing_enable().set(1);
    dp_tp.dp_link_training_pattern().set(dp_tp.kTrainingPattern1);
    dp_tp.WriteTo(reg_io);

    // Tell the sink device to look for the training pattern.
    dpcd::TrainingPatternSet tp_set;
    tp_set.training_pattern_set().set(tp_set.kTrainingPattern1);
    tp_set.scrambling_disable().set(1);
    if (!DpcdRequestLinkTraining(&dp_aux, &tp_set))
        return false;

    dpcd::Lane01Status lane01_status;
    // Number of times to poll with the same voltage level configured, as
    // specified by the DisplayPort spec.
    const int kPollsPerVoltageLevel = 5;
    // Time to wait before polling the registers for the result of the
    // first training step, as specified by the DisplayPort spec.
    const int kPollTimeUsec = 100;
    int poll_count = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::microseconds(kPollTimeUsec));

        // Did the sink device receive the signal successfully?
        if (!DpcdReadLaneStatus(&dp_aux, &lane01_status))
            return false;
        if (lane01_status.lane0_cr_done().get() && lane01_status.lane1_cr_done().get())
            break;
        // The training attempt has not succeeded yet.
        // TODO(MA-150): We are supposed to read the ADJUST_REQUEST_LANE0_1
        // DPCD register and tell the source device to produce a stronger
        // signal (higher voltage swing level, etc.) as instructed by the
        // sink device.
        if (++poll_count == kPollsPerVoltageLevel)
            return DRETF(false, "DP: Link training: clock recovery step failed");
    }

    // Link training stage 2.

    // Again, tell the source device to emit the training pattern.
    dp_tp.dp_link_training_pattern().set(dp_tp.kTrainingPattern2);
    dp_tp.WriteTo(reg_io);

    // Again, tell the sink device to look for the training pattern.
    tp_set.training_pattern_set().set(tp_set.kTrainingPattern2);
    if (!DpcdRequestLinkTraining(&dp_aux, &tp_set))
        return false;

    // Allow 400us for the second training step, as specified by the
    // DisplayPort spec.
    std::this_thread::sleep_for(std::chrono::microseconds(400));

    // Did the sink device receive the signal successfully?
    if (!DpcdReadLaneStatus(&dp_aux, &lane01_status))
        return false;
    if (!lane01_status.lane0_cr_done().get() || !lane01_status.lane1_cr_done().get())
        return DRETF(false, "DP: Link training: clock recovery regressed");
    if (!lane01_status.lane0_symbol_locked().get() || !lane01_status.lane1_symbol_locked().get())
        return DRETF(false, "DP: Link training: symbol lock failed");
    if (!lane01_status.lane0_channel_eq_done().get() ||
        !lane01_status.lane1_channel_eq_done().get())
        return DRETF(false, "DP: Link training: channel equalization failed");

    dp_tp.dp_link_training_pattern().set(dp_tp.kSendPixelData);
    dp_tp.WriteTo(reg_io);

    return true;
}

bool DoLinkTraining(RegisterIo* reg_io, int32_t ddi_number)
{
    bool result = LinkTrainingBody(reg_io, ddi_number);

    // Tell the sink device to end its link training attempt.
    //
    // If link training was successful, we need to do this so that the sink
    // device will accept pixel data from the source device.
    //
    // If link training was not successful, we want to do this so that
    // subsequent link training attempts can work.  If we don't unset this
    // register, subsequent link training attempts can also fail.  (This
    // can be important during development.  The sink device won't
    // necessarily get reset when the computer is reset.  This means that a
    // bad version of the driver can leave the sink device in a state where
    // good versions subsequently don't work.)
    uint32_t addr = DisplayPort::DPCD_TRAINING_PATTERN_SET;
    uint8_t reg_byte = 0;
    DpAuxChannel dp_aux(reg_io, ddi_number);
    if (!dp_aux.DpcdWrite(addr, &reg_byte, sizeof(reg_byte)))
        return DRETF(false, "Failure setting TRAINING_PATTERN_SET");

    return result;
}

// Convert ratio x/y into the form used by the Link/Data M/N ratio registers.
void CalculateRatio(uint32_t x, uint32_t y, uint32_t* m_out, uint32_t* n_out)
{
    // The exact denominator (N) value shouldn't matter too much.  Larger
    // values will tend to represent the ratio more accurately.  The value
    // must fit into a 24-bit register, so use 1 << 23.
    const uint32_t kDenominator = 1 << 23;
    *n_out = kDenominator;
    *m_out = static_cast<uint64_t>(x) * kDenominator / y;
}

} // namespace

bool DisplayPort::PartiallyBringUpDisplay(RegisterIo* reg_io, uint32_t ddi_number, BaseEdid* edid)
{
    // TODO(MA-150): Handle other DDIs.
    if (ddi_number != 2)
        return DRETF(false, "Only DDI C (DDI 2) is currently supported");

    uint32_t dpll_number = 1;

    // Transcoder B can only take input from Pipe B.
    uint32_t pipe_number = 1; // Pipe B
    uint32_t trans_num = 1;   // Transcoder B

    registers::PipeRegs pipe(pipe_number);
    registers::TranscoderRegs trans(trans_num);

    // Enable power for this DDI.
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(reg_io);
    power_well.ddi_c_io_power_request().set(1);
    power_well.WriteTo(reg_io);

    // Configure this DPLL to produce a suitable clock signal.
    auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(reg_io);
    dpll_ctrl1.dpll1_hdmi_mode().set(0);
    dpll_ctrl1.dpll1_ssc_enable().set(0);
    dpll_ctrl1.dpll1_link_rate().set(dpll_ctrl1.kLinkRate1350Mhz);
    dpll_ctrl1.dpll1_override().set(1);
    dpll_ctrl1.WriteTo(reg_io);

    // Enable this DPLL.
    auto lcpll2 = registers::Lcpll2Control::Get().FromValue(0);
    lcpll2.enable_dpll1().set(1);
    lcpll2.WriteTo(reg_io);

    // Configure this DDI to use the given DPLL as its clock source.
    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(reg_io);
    dpll_ctrl2.ddi_c_clock_select().set(dpll_number);
    dpll_ctrl2.ddi_c_select_override().set(1);
    dpll_ctrl2.WriteTo(reg_io);

    if (!DoLinkTraining(reg_io, ddi_number)) {
        magma::log(magma::LOG_WARNING, "DDI %d: DisplayPort link training failed", ddi_number);
        return false;
    }
    magma::log(magma::LOG_INFO, "DDI %d: DisplayPort link training succeeded", ddi_number);

    EdidTimingDesc* timing = &edid->preferred_timing;
    if (timing->pixel_clock_10khz == 0)
        return DRETF(false, "Timing descriptor not valid");

    // Pixel clock rate: The rate at which pixels are sent, in pixels per
    // second (Hz), divided by 10000.
    uint32_t pixel_clock_rate = timing->pixel_clock_10khz;

    uint32_t link_rate_mhz = 2700;
    // This is the rate at which bits are sent on a single DisplayPort
    // lane, in raw bits per second, divided by 10000.
    uint32_t link_raw_bit_rate = link_rate_mhz * 100;
    // Link symbol rate: The rate at which link symbols are sent on a
    // single DisplayPort lane.  A link symbol is 10 raw bits (using 8b/10b
    // encoding, which usually encodes an 8-bit data byte).
    uint32_t link_symbol_rate = link_raw_bit_rate / 10;

    uint32_t bits_per_pixel = 18; // 6 bits per color.
    uint32_t lane_count = 2;

    // Link M/N ratio: This is the ratio between two clock rates.  This
    // ratio is specified in the DisplayPort standard.  The ratio value is
    // sent across the DisplayPort link in the MSA (Main Stream Attribute)
    // data, and the sink device can use it or ignore it.
    //
    // This ratio is: The fraction of link symbol clock ticks that should
    // cause the pixel clock to tick.  Since DisplayPort does not allow
    // color depths of less than 8 bits per pixel, this ratio cannot be
    // more than 1.
    uint32_t link_m;
    uint32_t link_n;
    CalculateRatio(pixel_clock_rate, link_symbol_rate, &link_m, &link_n);

    // Data M/N ratio: This is the ratio between two bit rates.
    //
    // This ratio is: The fraction of the DisplayPort link capacity that is
    // occupied with pixel data.  This must always be less than 1, since we
    // can't use more than 100% of the link capacity.  This cannot be
    // exactly 1, since some of the link capacity is required for control
    // data.
    uint32_t pixel_bit_rate = pixel_clock_rate * bits_per_pixel;
    uint32_t total_link_bit_rate = link_symbol_rate * 8 * lane_count;
    uint32_t data_m;
    uint32_t data_n;
    CalculateRatio(pixel_bit_rate, total_link_bit_rate, &data_m, &data_n);

    auto data_m_reg = trans.DataM().FromValue(0);
    data_m_reg.tu_or_vcpayload_size().set(63); // Size of 64, minus 1.
    data_m_reg.data_m_value().set(data_m);
    data_m_reg.WriteTo(reg_io);

    auto data_n_reg = trans.DataN().FromValue(0);
    data_n_reg.data_n_value().set(data_n);
    data_n_reg.WriteTo(reg_io);

    auto link_m_reg = trans.LinkM().FromValue(0);
    link_m_reg.link_m_value().set(link_m);
    link_m_reg.WriteTo(reg_io);

    auto link_n_reg = trans.LinkN().FromValue(0);
    link_n_reg.link_n_value().set(link_n);
    link_n_reg.WriteTo(reg_io);

    uint32_t h_active = timing->horizontal_addressable() - 1;
    uint32_t h_sync_start = h_active + timing->horizontal_front_porch();
    uint32_t h_sync_end = h_sync_start + timing->horizontal_sync_pulse_width();
    uint32_t h_total = h_active + timing->horizontal_blanking();

    uint32_t v_active = timing->vertical_addressable() - 1;
    uint32_t v_sync_start = v_active + timing->vertical_front_porch();
    uint32_t v_sync_end = v_sync_start + timing->vertical_sync_pulse_width();
    uint32_t v_total = v_active + timing->vertical_blanking();

    auto h_total_reg = trans.HTotal().FromValue(0);
    h_total_reg.count_total().set(h_total);
    h_total_reg.count_active().set(h_active);
    h_total_reg.WriteTo(reg_io);
    auto v_total_reg = trans.VTotal().FromValue(0);
    v_total_reg.count_total().set(v_total);
    v_total_reg.count_active().set(v_active);
    v_total_reg.WriteTo(reg_io);

    auto h_sync_reg = trans.HSync().FromValue(0);
    h_sync_reg.sync_start().set(h_sync_start);
    h_sync_reg.sync_end().set(h_sync_end);
    h_sync_reg.WriteTo(reg_io);
    auto v_sync_reg = trans.VSync().FromValue(0);
    v_sync_reg.sync_start().set(v_sync_start);
    v_sync_reg.sync_end().set(v_sync_end);
    v_sync_reg.WriteTo(reg_io);

    // The Intel docs say that HBlank should be programmed with the same
    // values as HTotal.  Similarly, VBlank should be programmed with the
    // same values as VTotal.  (See
    // intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf,
    // p.932, p.962, p.974, p.980.)
    trans.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(reg_io);
    trans.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(reg_io);

    auto pipe_size = pipe.PipeSourceSize().FromValue(0);
    pipe_size.horizontal_source_size().set(h_active);
    pipe_size.vertical_source_size().set(v_active);
    pipe_size.WriteTo(reg_io);

    auto clock_select = trans.ClockSelect().FromValue(0);
    clock_select.trans_clock_select().set(ddi_number + 1);
    clock_select.WriteTo(reg_io);

    auto msa_misc = trans.MsaMisc().FromValue(0);
    msa_misc.sync_clock().set(1);
    msa_misc.WriteTo(reg_io);

    auto ddi_func = trans.DdiFuncControl().FromValue(0);
    ddi_func.trans_ddi_function_enable().set(1);
    ddi_func.ddi_select().set(ddi_number);
    ddi_func.trans_ddi_mode_select().set(ddi_func.kModeDisplayPortSst);
    ddi_func.bits_per_color().set(2);
    ddi_func.port_sync_mode_master_select().set(0);
    ddi_func.sync_polarity().set(1);
    ddi_func.port_sync_mode_enable().set(0);
    ddi_func.dp_vc_payload_allocate().set(0);
    ddi_func.dp_port_width_selection().set(1);
    ddi_func.WriteTo(reg_io);

    // TODO(MA-150): Allocate ranges of the plane buffer properly rather
    // than using the following fixed range.  This might involve checking
    // what ranges have already been allocated for displays that were set
    // up by the firmware's modesetting, or redoing the configuration of
    // those displays from scratch.
    auto buf_cfg = pipe.PlaneBufCfg().FromValue(0);
    buf_cfg.buffer_start().set(0x1be);
    buf_cfg.buffer_end().set(0x373);
    buf_cfg.WriteTo(reg_io);

    auto trans_conf = trans.Conf().FromValue(0);
    trans_conf.transcoder_enable().set(1);
    trans_conf.WriteTo(reg_io);

    auto plane_control = pipe.PlaneControl().FromValue(0);
    plane_control.plane_enable().set(1);
    plane_control.pipe_gamma_enable().set(1);
    plane_control.source_pixel_format().set(plane_control.kFormatRgb8888);
    plane_control.plane_gamma_disable().set(1);
    plane_control.WriteTo(reg_io);

    auto plane_size = pipe.PlaneSurfaceSize().FromValue(0);
    plane_size.width_minus_1().set(h_active);
    plane_size.height_minus_1().set(v_active);
    plane_size.WriteTo(reg_io);

    // TODO(MA-150): Plumb through the framebuffer's stride value.
    auto plane_stride = pipe.PlaneSurfaceStride().FromValue(0);
    plane_stride.stride().set(0x87);
    plane_stride.WriteTo(reg_io);

    // The following write arms the writes to the plane registers written
    // above.
    auto plane_addr = pipe.PlaneSurfaceAddress().FromValue(0);
    // TODO(MA-150): Plumb through the actual framebuffer address and use
    // that.  For now, the following address will display something that is
    // recognisable but misaligned, allowing us to check that the display
    // has come up.
    plane_addr.surface_base_address().set(0);
    plane_addr.WriteTo(reg_io);

    return true;
}

// This function partially implements bringing up a display, though not yet
// to the point where the display will display something.  It covers:
//  * reading EDID data
//  * doing DisplayPort link training
//
// We can test that functionality by running the driver on real hardware
// and eyeballing the log output.  The log output will be less necessary
// once we can bring up a display to display something.
void DisplayPort::PartiallyBringUpDisplays(RegisterIo* reg_io)
{
    uint32_t logged_count = 0;

    for (uint32_t ddi_number = 0; ddi_number < registers::DdiRegs::kDdiCount; ++ddi_number) {
        BaseEdid edid;
        // The following cast should be safe from C++ strict aliasing
        // problems because FetchEdidData() should act as if it writes its
        // results byte by byte.
        if (!FetchEdidData(reg_io, ddi_number, reinterpret_cast<uint8_t*>(&edid), sizeof(edid)))
            continue;

        if (!edid.valid_header()) {
            magma::log(magma::LOG_WARNING, "DDI %d: EDID: Read EDID data, but got bad header",
                       ddi_number);
        } else if (!edid.valid_checksum()) {
            magma::log(magma::LOG_WARNING, "DDI %d: EDID: Read EDID data, but got bad checksum",
                       ddi_number);
        } else {
            magma::log(magma::LOG_INFO,
                       "DDI %d: EDID: Read EDID data successfully, with correct header",
                       ddi_number);
            PartiallyBringUpDisplay(reg_io, ddi_number, &edid);
        }
        ++logged_count;
    }

    if (logged_count == 0)
        magma::log(magma::LOG_INFO, "EDID: Read EDID data for 0 DDIs");
}
