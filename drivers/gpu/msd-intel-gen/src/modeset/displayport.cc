// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modeset/displayport.h"
#include "magma_util/macros.h"
#include "register_io.h"
#include "registers.h"
#include "registers_ddi.h"
#include "registers_dpll.h"
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
    uint32_t data_reg = registers::DdiAuxData::GetOffset(ddi_number_);

    // Write the outgoing message to the hardware.
    DASSERT(request->size <= DpAuxMessage::kMaxTotalSize);
    for (uint32_t offset = 0; offset < request->size; offset += 4) {
        reg_io_->Write32(data_reg + offset, request->GetPackedWord(offset));
    }

    auto status = registers::DdiAuxControl::Get(ddi_number_).FromValue(0);
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
        auto status = registers::DdiAuxControl::Get(ddi_number_).ReadFrom(reg_io_);
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
    // request.  The spec says "A DP Source device is required to retry at
    // least seven times upon receiving AUX_DEFER before giving up the AUX
    // transaction", from section 2.7.7.1.5.6.1 in v1.3.  (AUX_DEFER
    // replies were in earlier versions, but v1.3 clarified the number of
    // retries required.)
    const int kMaxDefers = 8;

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

    // For now, we only support 2 DisplayPort lanes.
    // TODO(MA-150): We should also handle using 1 or 4 lanes.
    uint32_t dp_lane_count = 2;

    auto buf_ctl = registers::DdiBufControl::Get(ddi_number).FromValue(0);
    buf_ctl.ddi_buffer_enable().set(1);
    buf_ctl.dp_port_width_selection().set(dp_lane_count - 1);
    buf_ctl.WriteTo(reg_io);

    // Link training stage 1.

    // Tell the source device to emit the training pattern.
    auto dp_tp = registers::DdiDpTransportControl::Get(ddi_number).FromValue(0);
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

    // Allow 100us for the first training step, as specified by the
    // DisplayPort spec.
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Did the sink device receive the signal successfully?
    dpcd::Lane01Status lane01_status;
    if (!DpcdReadLaneStatus(&dp_aux, &lane01_status))
        return false;
    // TODO(MA-150): If the training attempts fail, we are supposed to try
    // again after telling the source device to produce a stronger signal
    // (higher voltage swing level, etc.).  This is not implemented yet.
    if (!lane01_status.lane0_cr_done().get() || !lane01_status.lane1_cr_done().get())
        return DRETF(false, "DP: Link training: clock recovery step failed");

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

} // namespace

bool DisplayPort::PartiallyBringUpDisplay(RegisterIo* reg_io, uint32_t ddi_number)
{
    // TODO(MA-150): Handle other DDIs.
    if (ddi_number != 2)
        return DRETF(false, "Only DDI C (DDI 2) is currently supported");

    uint32_t dpll_number = 1;

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
    if (!MSD_INTEL_ENABLE_MODESETTING) {
        magma::log(magma::LOG_INFO, "Modesetting code is disabled; "
                                    "build with \"packages/gn/gen.py ... "
                                    "--args msd_intel_enable_modesetting=true\" to enable");
        return;
    }

    uint32_t logged_count = 0;

    for (uint32_t ddi_number = 0; ddi_number < registers::Ddi::kDdiCount; ++ddi_number) {
        // Read enough just to test that we got the correct header.
        uint8_t buf[32];
        if (!FetchEdidData(reg_io, ddi_number, buf, sizeof(buf)))
            continue;

        static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        if (memcmp(buf, kEdidHeader, sizeof(kEdidHeader)) != 0) {
            magma::log(magma::LOG_WARNING, "DDI %d: EDID: Read EDID data, but got bad header",
                       ddi_number);
        } else {
            magma::log(magma::LOG_INFO,
                       "DDI %d: EDID: Read EDID data successfully, with correct header",
                       ddi_number);
            PartiallyBringUpDisplay(reg_io, ddi_number);
        }
        ++logged_count;
    }

    if (logged_count == 0)
        magma::log(magma::LOG_INFO, "EDID: Read EDID data for 0 DDIs");
}
