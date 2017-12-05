// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>

#include <string.h>
#include <endian.h>

#include "dp-display.h"
#include "edid.h"
#include "macros.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

namespace {

// TODO(ZX-1416): Handle more HW variants
// high dword, low dword, i_boost
uint32_t ddi_buf_trans_skl_u[9][3] = {
    { 0x000000a2, 0x0000201b, 0x01 },
    { 0x00000088, 0x00005012, 0x01 },
    { 0x000000cd, 0x80007011, 0x01 },
    { 0x000000c0, 0x80009010, 0x01 },
    { 0x0000009d, 0x0000201b, 0x01 },
    { 0x000000c0, 0x80005012, 0x01 },
    { 0x000000c0, 0x80007011, 0x01 },
    { 0x00000088, 0x00002016, 0x01 },
    { 0x000000c0, 0x80005012, 0x01 },
};

} // namespace

// Aux port functions

namespace i915 {

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

// This represents a message sent over DisplayPort's Aux channel, including
// reply messages.
class DpAuxMessage {
public:
    // Sizes in bytes.  DisplayPort Aux messages are quite small.
    static constexpr uint32_t kMaxTotalSize = 20;
    static constexpr uint32_t kMaxBodySize = 16;

    uint8_t data[kMaxTotalSize];
    uint32_t size;

    // Fill out the header of a DisplayPort Aux message.  For write operations,
    // |body_size| is the size of the body of the message to send.  For read
    // operations, |body_size| is the size of our receive buffer.
    bool SetDpAuxHeader(uint32_t addr, uint32_t dp_cmd, uint32_t body_size) {
        if (body_size > DpAuxMessage::kMaxBodySize) {
            zxlogf(ERROR, "DP aux: Message too large\n");
            return false;
        }
        // Addresses should fit into 20 bits.
        if (addr >= (1 << 20)) {
            zxlogf(ERROR, "DP aux: Address is too large: 0x%x\n", addr);
            return false;
        }
        // For now, we don't handle messages with empty bodies.  (However, they
        // can be used for checking whether there is an I2C device at a given
        // address.)
        if (body_size == 0) {
            zxlogf(ERROR, "DP aux: Empty message not supported\n");
            return false;
        }
        data[0] = static_cast<uint8_t>((dp_cmd << 4) | ((addr >> 16) & 0xf));
        data[1] = static_cast<uint8_t>(addr >> 8);
        data[2] = static_cast<uint8_t>(addr);
        // For writes, the size of the message will be encoded twice:
        //  * The msg->size field contains the total message size (header and
        //    body).
        //  * If the body of the message is non-empty, the header contains an
        //    extra field specifying the body size (in bytes minus 1).
        // For reads, the message to send is a header only.
        size = 4;
        data[3] = static_cast<uint8_t>(body_size - 1);
        return true;
}

};

bool DpDisplay::SendDpAuxMsg(const DpAuxMessage& request, DpAuxMessage* reply, bool* timeout_result) {
    *timeout_result = false;

    registers::DdiRegs ddi_regs(ddi());
    uint32_t data_reg = ddi_regs.DdiAuxData().addr();

    // Write the outgoing message to the hardware.
    for (uint32_t offset = 0; offset < request.size; offset += 4) {
        // For some reason intel made these data registers big endian...
        const uint32_t* data = reinterpret_cast<const uint32_t*>(request.data + offset);
        mmio_space()->Write32(data_reg + offset, htobe32(*data));
    }

    auto status = ddi_regs.DdiAuxControl().ReadFrom(mmio_space());
    status.message_size().set(request.size);
    // Reset R/W Clear bits
    status.done().set(1);
    status.timeout().set(1);
    status.rcv_error().set(1);
    // The documentation says to not use setting 0 (400us), so use 1 (600us).
    status.timeout_timer_value().set(1);
    // TODO(ZX-1416): Support interrupts
    status.interrupt_on_done().set(1);
    // Send busy starts the transaction
    status.send_busy().set(1);
    status.WriteTo(mmio_space());

    // Poll for the reply message.
    const int kNumTries = 10000;
    for (int tries = 0; tries < kNumTries; ++tries) {
        auto status = ddi_regs.DdiAuxControl().ReadFrom(mmio_space());
        if (!status.send_busy().get()) {
            if (status.timeout().get()) {
                *timeout_result = true;
                return false;
            }
            if (status.rcv_error().get()) {
                zxlogf(ERROR, "DP aux: rcv error\n");
                return false;
            }
            if (!status.done().get()) {
                continue;
            }

            reply->size = status.message_size().get();
            if (!reply->size || reply->size > DpAuxMessage::kMaxTotalSize) {
                zxlogf(ERROR, "DP aux: Invalid reply size %d\n", reply->size);
                return false;
            }
            // Read the reply message from the hardware.
            for (uint32_t offset = 0; offset < reply->size; offset += 4) {
                // For some reason intel made these data registers big endian...
                *reinterpret_cast<uint32_t*>(reply->data + offset) =
                        be32toh(mmio_space()->Read32(data_reg + offset));
            }
            return true;
        }
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
    }
    zxlogf(ERROR, "DP aux: No reply after %d tries\n", kNumTries);
    return false;
}

bool DpDisplay::SendDpAuxMsgWithRetry(const DpAuxMessage& request, DpAuxMessage* reply) {
    // If the DisplayPort sink device isn't ready to handle an Aux message,
    // it can return an AUX_DEFER reply, which means we should retry the
    // request. The spec added a requirement for >=7 defer retries in v1.3,
    // but there are no requirements before that nor is there a max value. 16
    // retries is pretty arbitrary and might need to be increased for slower
    // displays.
    const int kMaxDefers = 16;

    // Per table 2-43 in v1.1a, we need to retry >3 times, since some
    // DisplayPort sink devices time out on the first DP aux request
    // but succeed on later requests.
    const int kMaxTimeouts = 3;

    unsigned defers_seen = 0;
    unsigned timeouts_seen = 0;

    for (;;) {
        bool timeout_result;
        if (!SendDpAuxMsg(request, reply, &timeout_result)) {
            if (timeout_result) {
                if (++timeouts_seen == kMaxTimeouts) {
                    zxlogf(ERROR, "DP aux: Got too many timeouts (%d)\n", kMaxTimeouts);
                    return false;
                }
                // Retry on timeout.
                continue;
            }
            // We do not retry if sending the raw message failed for
            // an unexpected reason.
            return false;
        }

        uint8_t header_byte = reply->data[0];
        uint8_t padding = header_byte & 0xf;
        uint8_t status = static_cast<uint8_t>(header_byte >> 4);
        // Sanity check: The padding should be zero.  If it's not, we
        // shouldn't return an error, in case this space gets used for some
        // later extension to the protocol.  But report it, in case this
        // indicates some problem.
        if (padding) {
            zxlogf(INFO,
                   "DP aux: Reply header padding is non-zero (header byte: 0x%x)\n", header_byte);
        }

        switch (status) {
            case DP_REPLY_AUX_ACK:
                // The AUX_ACK implies that we got an I2C ACK too.
                return true;
            case DP_REPLY_AUX_DEFER:
                if (++defers_seen == kMaxDefers) {
                    zxlogf(ERROR , "DP aux: Received too many AUX DEFERs (%d)\n", kMaxDefers);
                    return false;
                }
                // Go around the loop again to retry.
                continue;
            case DP_REPLY_AUX_NACK:
                zxlogf(ERROR, "DP aux: Reply was not an ack (got AUX_NACK)\n");
                return false;
            case DP_REPLY_I2C_NACK:
                zxlogf(ERROR, "DP aux: Reply was not an ack (got I2C_NACK)\n");
                return false;
            case DP_REPLY_I2C_DEFER:
                // TODO(ZX-1416): Implement handling of I2C_DEFER.
                zxlogf(ERROR, "DP aux: Received I2C_DEFER (not implemented)\n");
                return false;
            default:
                // We got a reply that is not defined by the DisplayPort spec.
                zxlogf(ERROR, "DP aux: Unrecognized reply (header byte: 0x%x)\n", header_byte);
                return false;
        }
    }
}

bool DpDisplay::DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size) {
    while (size > 0) {
        uint32_t chunk_size = MIN(size, DpAuxMessage::kMaxBodySize);
        uint32_t bytes_read = 0;
        if (!DpAuxReadChunk(dp_cmd, addr, buf, chunk_size, &bytes_read)) {
            return false;
        }
        if (bytes_read == 0) {
            // We failed to make progress on the last call.  To avoid the
            // risk of getting an infinite loop from that happening
            // continually, we return.
            return false;
        }
        buf += bytes_read;
        size -= bytes_read;
    }
    return true;
}

bool DpDisplay::DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                               uint32_t* size_out) {
    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!msg.SetDpAuxHeader(addr, dp_cmd, size_in) || !SendDpAuxMsgWithRetry(msg, &reply)) {
        return false;
    }
    uint32_t bytes_read = reply.size - 1;
    if (bytes_read > size_in) {
        zxlogf(ERROR, "DP aux read: Reply was larger than requested\n");
        return false;
    }
    memcpy(buf, &reply.data[1], bytes_read);
    *size_out = bytes_read;
    return true;
}

bool DpDisplay::DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, uint32_t size) {
    // Implement this if it's ever needed
    ZX_DEBUG_ASSERT_MSG(size <= 16, "message too large");

    DpAuxMessage msg;
    DpAuxMessage reply;
    if (!msg.SetDpAuxHeader(addr, dp_cmd, size)) {
        return false;
    }
    memcpy(&msg.data[4], buf, size);
    msg.size = size + 4;
    if (!SendDpAuxMsgWithRetry(msg, &reply)) {
        return false;
    }
    // TODO(ZX-1416): Handle the case where the hardware did a short write,
    // for which we could send the remaining bytes.
    if (reply.size != 1) {
        zxlogf(ERROR, "DP aux write: Unexpected reply size\n");
        return false;
    }
    return true;
}

bool DpDisplay::I2cRead(uint32_t addr, uint8_t* buf, uint32_t size) {
    return DpAuxRead(DP_REQUEST_I2C_READ, addr, buf, size);
}

bool DpDisplay::DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size) {
    return DpAuxRead(DP_REQUEST_NATIVE_READ, addr, buf, size);
}

bool DpDisplay::I2cWrite(uint32_t addr, uint8_t* buf, uint32_t size) {
    return DpAuxWrite(DP_REQUEST_I2C_WRITE, addr, buf, size);
}

bool DpDisplay::DpcdWrite(uint32_t addr, const uint8_t* buf, uint32_t size) {
    return DpAuxWrite(DP_REQUEST_NATIVE_WRITE, addr, buf, size);
}

// Link training functions

// Tell the sink device to start link training.
bool DpDisplay::DpcdRequestLinkTraining(const dpcd::TrainingPatternSet& tp_set,
                                        const dpcd::TrainingLaneSet lane[]) {
    // The DisplayPort spec says that we are supposed to write these
    // registers with a single operation: "The AUX CH burst write must be
    // used for writing to TRAINING_LANEx_SET bytes of the enabled lanes."
    // (From section 3.5.1.3, "Link Training", in v1.1a.)
    uint8_t reg_bytes[1 + dp_lane_count_];
    reg_bytes[0] = static_cast<uint8_t>(tp_set.reg_value());
    for (unsigned i = 0; i < dp_lane_count_; i++) {
        reg_bytes[i + 1] = static_cast<uint8_t>(lane[i].reg_value());
    }
    constexpr int kAddr = dpcd::DPCD_TRAINING_PATTERN_SET;
    static_assert(kAddr + 1 == dpcd::DPCD_TRAINING_LANE0_SET, "");
    static_assert(kAddr + 2 == dpcd::DPCD_TRAINING_LANE1_SET, "");
    static_assert(kAddr + 3 == dpcd::DPCD_TRAINING_LANE2_SET, "");
    static_assert(kAddr + 4 == dpcd::DPCD_TRAINING_LANE3_SET, "");

    if (!DpcdWrite(kAddr, reg_bytes, 1 + dp_lane_count_)) {
        zxlogf(ERROR, "Failure setting TRAINING_PATTERN_SET\n");
        return false;
    }

    return true;
}

template<uint32_t addr, typename T>
bool DpDisplay::DpcdReadPairedRegs(registers::RegisterBase<T>* regs) {
    static_assert(addr == dpcd::DPCD_LANE0_1_STATUS || addr == dpcd::DPCD_ADJUST_REQUEST_LANE0_1,
                  "Bad register address");
    uint32_t num_bytes = dp_lane_count_ == 4 ? 2 : 1;
    uint8_t reg_byte[num_bytes];
    if (!DpcdRead(addr, reg_byte, num_bytes)) {
        zxlogf(ERROR, "Failure reading addr %d\n", addr);
        return false;
    }

    for (unsigned i = 0; i < dp_lane_count_; i++) {
        regs[i].set_reg_value(reg_byte[i / 2]);
    }

    return true;
}

bool DpDisplay::DpcdHandleAdjustRequest(dpcd::TrainingLaneSet* training,
                                        dpcd::AdjustRequestLane* adjust) {
    bool voltage_change = false;
    uint8_t v = 0;
    uint8_t pe = 0;
    for (unsigned i = 0; i < dp_lane_count_; i++) {
        if (adjust[i].voltage_swing(i).get() > v) {
            v = static_cast<uint8_t>(adjust[i].voltage_swing(i).get());
        }
        if (adjust[i].pre_emphasis(i).get() > pe) {
            pe = static_cast<uint8_t>(adjust[i].pre_emphasis(i).get());
        }
    }

    // In the Recommended buffer translation programming for DisplayPort from the intel display
    // doc, the max voltage swing is 2 and the max (voltage swing + pre-emphasis) is 3. According
    // to the v1.1a of the DP docs, if v + pe is too large then v should be reduced to the highest
    // supported value for the pe level (section 3.5.1.3)
    static constexpr uint32_t kMaxVPlusPe = 3;
    // TODO(ZX-1416): max v for eDP is 3
    static constexpr uint32_t kMaxV = 2;
    if (v + pe > kMaxVPlusPe) {
        v = static_cast<uint8_t>(kMaxVPlusPe - pe);
    }
    if (v > kMaxV) {
        v = kMaxV;
    }

    for (unsigned i = 0; i < dp_lane_count_; i++) {
        voltage_change |= (training[i].voltage_swing_set().get() != v);
        training[i].voltage_swing_set().set(v);
        training[i].max_swing_reached().set(v == kMaxV);
        training[i].pre_emphasis_set().set(pe);
        training[i].max_pre_emphasis_set().set(pe + v == kMaxVPlusPe);
    }

    // Compute the index into the programmed table
    int level;
    if (v == 0) {
        level = pe;
    } else if (v == 1) {
        level = 4 + pe;
    } else if (v == 2) {
        level = 7 + pe;
    } else {
        level = 9;
    }

    registers::DdiRegs ddi_regs(ddi());
    auto buf_ctl = ddi_regs.DdiBufControl().ReadFrom(mmio_space());
    buf_ctl.dp_vswing_emp_sel().set(level);
    buf_ctl.WriteTo(mmio_space());

    return voltage_change;
}

bool DpDisplay::LinkTrainingSetup() {
    registers::DdiRegs ddi_regs(ddi());

    // TODO(ZX-1416): set SET_POWER dpcd field (0x600)

    uint8_t max_lc_byte;
    if (!DpcdRead(dpcd::DPCD_COUNT_SET, &max_lc_byte, 1)) {
        zxlogf(ERROR, "Failed to read lane count\n");
        return false;
    }
    dpcd::LaneCount max_lc;
    max_lc.set_reg_value(max_lc_byte);
    dp_lane_count_ = max_lc.lane_count_set().get();

    // Tell the source device to emit the training pattern.
    auto dp_tp = ddi_regs.DdiDpTransportControl().ReadFrom(mmio_space());
    dp_tp.transport_enable().set(1);
    dp_tp.transport_mode_select().set(0);
    dp_tp.enhanced_framing_enable().set(max_lc.enhanced_frame_enabled().get());
    dp_tp.dp_link_training_pattern().set(dp_tp.kTrainingPattern1);
    dp_tp.WriteTo(mmio_space());

    // Configure ddi voltage swing
    for (unsigned i = 0; i < 9; i++) {
        auto ddi_buf_trans_hi = ddi_regs.DdiBufTransHi(i).ReadFrom(mmio_space());
        auto ddi_buf_trans_lo = ddi_regs.DdiBufTransLo(i).ReadFrom(mmio_space());
        ddi_buf_trans_hi.set_reg_value(ddi_buf_trans_skl_u[i][0]);
        ddi_buf_trans_lo.set_reg_value(ddi_buf_trans_skl_u[i][1]);
        ddi_buf_trans_hi.WriteTo(mmio_space());
        ddi_buf_trans_lo.WriteTo(mmio_space());
    }
    auto disio_cr_tx_bmu = registers::DisplayIoCtrlRegTxBmu::Get().ReadFrom(mmio_space());
    disio_cr_tx_bmu.disable_balance_leg().set(0);
    disio_cr_tx_bmu.tx_balance_leg_select(ddi()).set(1);
    disio_cr_tx_bmu.WriteTo(mmio_space());

    // Enable and wait for DDI_BUF_CTL
    auto buf_ctl = ddi_regs.DdiBufControl().ReadFrom(mmio_space());
    buf_ctl.ddi_buffer_enable().set(1);
    buf_ctl.dp_vswing_emp_sel().set(0);
    buf_ctl.dp_port_width_selection().set(dp_lane_count_ - 1);
    buf_ctl.WriteTo(mmio_space());
    zx_nanosleep(zx_deadline_after(ZX_USEC(518)));

    // Configure the bandwidth and lane count settings
    dpcd::LinkBw bw_setting;
    bw_setting.link_bw_set().set(bw_setting.k2700Mbps); // kLinkRateMhz
    dpcd::LaneCount lc_setting;
    lc_setting.lane_count_set().set(dp_lane_count_);
    lc_setting.enhanced_frame_enabled().set(max_lc.enhanced_frame_enabled().get());
    uint8_t settings[2];
    settings[0] = static_cast<uint8_t>(bw_setting.reg_value());
    settings[1] = static_cast<uint8_t>(lc_setting.reg_value());
    if (!DpcdWrite(dpcd::DPCD_LINK_BW_SET, settings, 2)) {
        zxlogf(ERROR, "DP: Link training: failed to configure settings\n");
        return false;
    }
    return true;
}

// Number of times to poll with the same voltage level configured, as
// specified by the DisplayPort spec.
static const int kPollsPerVoltageLevel = 5;

bool DpDisplay::LinkTrainingStage1(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes) {
    // Tell the sink device to look for the training pattern.
    tp_set->training_pattern_set().set(tp_set->kTrainingPattern1);
    tp_set->scrambling_disable().set(1);

    dpcd::AdjustRequestLane adjust_req[dp_lane_count_];
    dpcd::LaneStatus lane_status[dp_lane_count_];

    int poll_count = 0;
    for (;;) {
        if (!DpcdRequestLinkTraining(*tp_set, lanes)) {
            return false;
        }

        // Wait 100us before polling the registers for the result of the
        // first training step, as specified by the DisplayPort spec.
        zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

        // Did the sink device receive the signal successfully?
        if (!DpcdReadPairedRegs<dpcd::DPCD_LANE0_1_STATUS, dpcd::LaneStatus>(lane_status)) {
            return false;
        }
        bool done = true;
        for (unsigned i = 0; i < dp_lane_count_; i++) {
            done &= lane_status[i].lane_cr_done(i).get();
        }
        if (done) {
            break;
        }

        for (unsigned i = 0; i < dp_lane_count_; i++) {
            if (lanes[i].max_swing_reached().get()) {
                zxlogf(ERROR, "DP: Link training: max voltage swing reached\n");
                return false;
            }
        }

        if (!DpcdReadPairedRegs<dpcd::DPCD_ADJUST_REQUEST_LANE0_1,
                                dpcd::AdjustRequestLane>(adjust_req)) {
            return false;
        }

        if (DpcdHandleAdjustRequest(lanes, adjust_req)) {
            poll_count = 0;
        } else if (++poll_count == kPollsPerVoltageLevel) {
            zxlogf(ERROR, "DP: Link training: clock recovery step failed\n");
            return false;
        }
    }

    return true;
}

bool DpDisplay::LinkTrainingStage2(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes) {
    registers::DdiRegs ddi_regs(ddi());
    auto dp_tp = ddi_regs.DdiDpTransportControl().ReadFrom(mmio_space());

    dpcd::AdjustRequestLane adjust_req[dp_lane_count_];
    dpcd::LaneStatus lane_status[dp_lane_count_];

    dp_tp.dp_link_training_pattern().set(dp_tp.kTrainingPattern2);
    dp_tp.WriteTo(mmio_space());

    tp_set->training_pattern_set().set(tp_set->kTrainingPattern2);
    int poll_count = 0;
    for (;;) {
        // lane0_training and lane1_training can change in the loop
        if (!DpcdRequestLinkTraining(*tp_set, lanes)) {
            return false;
        }

        // Allow 400us for the second training step, as specified by the
        // DisplayPort spec.
        zx_nanosleep(zx_deadline_after(ZX_USEC(400)));

        // Did the sink device receive the signal successfully?
        if (!DpcdReadPairedRegs<dpcd::DPCD_LANE0_1_STATUS, dpcd::LaneStatus>(lane_status)) {
            return false;
        }
        for (unsigned i = 0; i < dp_lane_count_; i++) {
            if (!lane_status[i].lane_cr_done(i).get()) {
                zxlogf(ERROR, "DP: Link training: clock recovery regressed\n");
                return false;
            }
        }

        bool symbol_lock_done = true;
        bool channel_eq_done = true;
        for (unsigned i = 0; i < dp_lane_count_; i++) {
            symbol_lock_done &= lane_status[i].lane_symbol_locked(i).get();
            channel_eq_done &= lane_status[i].lane_channel_eq_done(i).get();
        }
        if (symbol_lock_done && channel_eq_done) {
            break;
        }

        // The training attempt has not succeeded yet.
        if (++poll_count == kPollsPerVoltageLevel) {
            if (symbol_lock_done) {
                zxlogf(ERROR, "DP: Link training: symbol lock failed\n");
                return false;
            } else {
                zxlogf(ERROR, "DP: Link training: channel equalization failed\n");
                return false;
            }
        }

        if (!DpcdReadPairedRegs<dpcd::DPCD_ADJUST_REQUEST_LANE0_1,
                                dpcd::AdjustRequestLane>(adjust_req)) {
            return false;
        }
        DpcdHandleAdjustRequest(lanes, adjust_req);
    }

    dp_tp.dp_link_training_pattern().set(dp_tp.kSendPixelData);
    dp_tp.WriteTo(mmio_space());

    return true;
}

bool DpDisplay::DoLinkTraining()
{
    // TODO(ZX-1416): If either of the two training steps fails, we're
    // supposed to try with a reduced bit rate.
    bool result = LinkTrainingSetup();
    if (result) {
        dpcd::TrainingPatternSet tp_set;
        dpcd::TrainingLaneSet lanes[dp_lane_count_];
        result = LinkTrainingStage1(&tp_set, lanes) && LinkTrainingStage2(&tp_set, lanes);
    }

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
    uint32_t addr = dpcd::DPCD_TRAINING_PATTERN_SET;
    uint8_t reg_byte = 0;
    if (!DpcdWrite(addr, &reg_byte, sizeof(reg_byte))) {
        zxlogf(ERROR, "Failure setting TRAINING_PATTERN_SET\n");
        return false;
    }

    return result;
}

} // namespace i915

namespace {

// Convert ratio x/y into the form used by the Link/Data M/N ratio registers.
void CalculateRatio(uint32_t x, uint32_t y, uint32_t* m_out, uint32_t* n_out) {
    // The exact denominator (N) value shouldn't matter too much.  Larger
    // values will tend to represent the ratio more accurately.  The value
    // must fit into a 24-bit register, so use 1 << 23.
    const uint32_t kDenominator = 1 << 23;
    *n_out = kDenominator;
    *m_out = static_cast<uint32_t>(static_cast<uint64_t>(x) * kDenominator / y);
}

} // namespace

namespace i915 {

DpDisplay::DpDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe)
        : DisplayDevice(controller, ddi, pipe) { }

bool DpDisplay::Init(zx_display_info* info) {
    if (!ResetPipe() || !ResetDdi()) {
        return false;
    }

    registers::BaseEdid edid;
    if (!LoadEdid(&edid)) {
        return false;
    }

    if (pipe() == registers::PIPE_A && ddi() == registers::DDI_A && !EnablePowerWell2()) {
        return false;
    }

    // TODO(ZX-1416): Parameterized these and update what references them
    static constexpr uint32_t kLinkRateMhz = 2700;
    static constexpr uint32_t kPixelFormat = ZX_PIXEL_FORMAT_RGB_x888;

    registers::TranscoderRegs trans(pipe());

    // Configure this DPLL to produce a suitable clock signal.
    auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(mmio_space());
    dpll_ctrl1.dpll_hdmi_mode(dpll()).set(0);
    dpll_ctrl1.dpll_ssc_enable(dpll()).set(0);
    dpll_ctrl1.dpll_link_rate(dpll()).set(dpll_ctrl1.kLinkRate1350Mhz); // kLinkRateMhz
    dpll_ctrl1.dpll_override(dpll()).set(1);
    dpll_ctrl1.WriteTo(mmio_space());
    dpll_ctrl1.ReadFrom(mmio_space()); // Posting read

    // Enable this DPLL and wait for it to lock
    auto dpll_enable = registers::DpllEnable::Get(dpll()).ReadFrom(mmio_space());
    dpll_enable.enable_dpll().set(1);
    dpll_enable.WriteTo(mmio_space());
    if (!WAIT_ON_MS(registers::DpllStatus
            ::Get().ReadFrom(mmio_space()).dpll_lock(dpll()).get(), 5)) {
        zxlogf(ERROR, "DPLL failed to lock\n");
        return false;
    }

    // Configure this DDI to use the given DPLL as its clock source.
    auto dpll_ctrl2 = registers::DpllControl2::Get().ReadFrom(mmio_space());
    dpll_ctrl2.ddi_clock_select(ddi()).set(dpll());
    dpll_ctrl2.ddi_select_override(ddi()).set(1);
    dpll_ctrl2.ddi_clock_off(ddi()).set(0);
    dpll_ctrl2.WriteTo(mmio_space());

    // Enable power for this DDI.
    auto power_well = registers::PowerWellControl2::Get().ReadFrom(mmio_space());
    power_well.ddi_io_power_request(ddi()).set(1);
    power_well.WriteTo(mmio_space());
    if (!WAIT_ON_US(registers::PowerWellControl2
            ::Get().ReadFrom(mmio_space()) .ddi_io_power_state(ddi()).get(), 20)) {
        zxlogf(ERROR, "Failed to enable IO power for ddi\n");
        return false;
    }

    // Do link training
    if (!DoLinkTraining()) {
        zxlogf(ERROR, "DDI %d: DisplayPort link training failed\n", ddi());
        return false;
    }

    // Configure Transcoder Clock Select
    auto clock_select = trans.ClockSelect().ReadFrom(mmio_space());
    clock_select.trans_clock_select().set(ddi() + 1);
    clock_select.WriteTo(mmio_space());

    registers::EdidTimingDesc* timing = &edid.preferred_timing;

    // Pixel clock rate: The rate at which pixels are sent, in pixels per
    // second (Hz), divided by 10000.
    uint32_t pixel_clock_rate = timing->pixel_clock_10khz;

    // This is the rate at which bits are sent on a single DisplayPort
    // lane, in raw bits per second, divided by 10000.
    uint32_t link_raw_bit_rate = kLinkRateMhz * 100;
    // Link symbol rate: The rate at which link symbols are sent on a
    // single DisplayPort lane.  A link symbol is 10 raw bits (using 8b/10b
    // encoding, which usually encodes an 8-bit data byte).
    uint32_t link_symbol_rate = link_raw_bit_rate / 10;

    uint32_t bits_per_pixel = 24; // kPixelFormat

    // Configure ratios between pixel clock/bit rate and symbol clock/bit rate
    uint32_t link_m;
    uint32_t link_n;
    CalculateRatio(pixel_clock_rate, link_symbol_rate, &link_m, &link_n);

    uint32_t pixel_bit_rate = pixel_clock_rate * bits_per_pixel;
    uint32_t total_link_bit_rate = link_symbol_rate * 8 * dp_lane_count_;
    uint32_t data_m;
    uint32_t data_n;
    CalculateRatio(pixel_bit_rate, total_link_bit_rate, &data_m, &data_n);

    auto data_m_reg = trans.DataM().FromValue(0);
    data_m_reg.tu_or_vcpayload_size().set(63); // Size - 1, default TU size is 64
    data_m_reg.data_m_value().set(data_m);
    data_m_reg.WriteTo(mmio_space());

    auto data_n_reg = trans.DataN().FromValue(0);
    data_n_reg.data_n_value().set(data_n);
    data_n_reg.WriteTo(mmio_space());

    auto link_m_reg = trans.LinkM().FromValue(0);
    link_m_reg.link_m_value().set(link_m);
    link_m_reg.WriteTo(mmio_space());

    auto link_n_reg = trans.LinkN().FromValue(0);
    link_n_reg.link_n_value().set(link_n);
    link_n_reg.WriteTo(mmio_space());

    // Configure the rest of the transcoder
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
    h_total_reg.WriteTo(mmio_space());
    auto v_total_reg = trans.VTotal().FromValue(0);
    v_total_reg.count_total().set(v_total);
    v_total_reg.count_active().set(v_active);
    v_total_reg.WriteTo(mmio_space());

    auto h_sync_reg = trans.HSync().FromValue(0);
    h_sync_reg.sync_start().set(h_sync_start);
    h_sync_reg.sync_end().set(h_sync_end);
    h_sync_reg.WriteTo(mmio_space());
    auto v_sync_reg = trans.VSync().FromValue(0);
    v_sync_reg.sync_start().set(v_sync_start);
    v_sync_reg.sync_end().set(v_sync_end);
    v_sync_reg.WriteTo(mmio_space());

    // The Intel docs say that H/VBlank should be programmed with the same H/VTotal
    trans.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(mmio_space());
    trans.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(mmio_space());

    auto msa_misc = trans.MsaMisc().FromValue(0);
    msa_misc.sync_clock().set(1);
    msa_misc.bits_per_color().set(msa_misc.k8Bbc); // kPixelFormat
    msa_misc.color_format().set(msa_misc.kRgb); // kPixelFormat
    msa_misc.WriteTo(mmio_space());

    auto ddi_func = trans.DdiFuncControl().ReadFrom(mmio_space());
    ddi_func.trans_ddi_function_enable().set(1);
    ddi_func.ddi_select().set(ddi());
    ddi_func.trans_ddi_mode_select().set(ddi_func.kModeDisplayPortSst);
    ddi_func.bits_per_color().set(ddi_func.k8bbc); // kPixelFormat
    ddi_func.sync_polarity().set(edid.preferred_timing.vsync_polarity().get() << 1
                                | edid.preferred_timing.hsync_polarity().get());
    ddi_func.port_sync_mode_enable().set(0);
    ddi_func.dp_vc_payload_allocate().set(0);
    ddi_func.dp_port_width_selection().set(dp_lane_count_ - 1);
    ddi_func.WriteTo(mmio_space());

    auto trans_conf = trans.Conf().FromValue(0);
    trans_conf.transcoder_enable().set(1);
    trans_conf.interlaced_mode().set(edid.preferred_timing.interlaced().get());
    trans_conf.WriteTo(mmio_space());

    // Configure the pipe
    registers::PipeRegs pipe_regs(pipe());

    auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
    pipe_size.horizontal_source_size().set(h_active);
    pipe_size.vertical_source_size().set(v_active);
    pipe_size.WriteTo(mmio_space());


    auto plane_control = pipe_regs.PlaneControl().FromValue(0);
    plane_control.plane_enable().set(1);
    plane_control.source_pixel_format().set(plane_control.kFormatRgb8888); // kPixelFormat
    plane_control.tiled_surface().set(plane_control.kLinear);
    plane_control.WriteTo(mmio_space());

    auto plane_size = pipe_regs.PlaneSurfaceSize().FromValue(0);
    plane_size.width_minus_1().set(h_active);
    plane_size.height_minus_1().set(v_active);
    plane_size.WriteTo(mmio_space());

    info->width = edid.preferred_timing.horizontal_addressable();
    info->height = edid.preferred_timing.vertical_addressable();
    info->stride = ROUNDUP(info->width, registers::PlaneSurfaceStride::kLinearStrideChunkSize);
    info->format = kPixelFormat;
    info->pixelsize = ZX_PIXEL_FORMAT_BYTES(info->format);

    return true;
}

} // namespace i915
