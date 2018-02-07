// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>

#include <endian.h>
#include <string.h>
#include <zircon/assert.h>

#include "dp-display.h"
#include "edid.h"
#include "intel-i915.h"
#include "macros.h"
#include "pci-ids.h"
#include "registers.h"
#include "registers-ddi.h"
#include "registers-dpll.h"
#include "registers-pipe.h"
#include "registers-transcoder.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

namespace i915 {

// Recommended DDI buffer translation programming values

struct ddi_buf_trans_entry {
    uint32_t high_dword;
    uint32_t low_dword;
};

const ddi_buf_trans_entry dp_ddi_buf_trans_skl_hs[9] = {
    { 0x000000a0, 0x00002016 },
    { 0x0000009b, 0x00005012 },
    { 0x00000088, 0x00007011 },
    { 0x000000c0, 0x80009010 },
    { 0x0000009b, 0x00002016 },
    { 0x00000088, 0x00005012 },
    { 0x000000c0, 0x80007011 },
    { 0x000000df, 0x00002016 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry dp_ddi_buf_trans_skl_y[9] = {
    { 0x000000a2, 0x00000018 },
    { 0x00000088, 0x00005012 },
    { 0x000000cd, 0x80007011 },
    { 0x000000c0, 0x80009010 },
    { 0x0000009d, 0x00000018 },
    { 0x000000c0, 0x80005012 },
    { 0x000000c0, 0x80007011 },
    { 0x00000088, 0x00000018 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry dp_ddi_buf_trans_skl_u[9] = {
    { 0x000000a2, 0x0000201b },
    { 0x00000088, 0x00005012 },
    { 0x000000cd, 0x80007011 },
    { 0x000000c0, 0x80009010 },
    { 0x0000009d, 0x0000201b },
    { 0x000000c0, 0x80005012 },
    { 0x000000c0, 0x80007011 },
    { 0x00000088, 0x00002016 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry dp_ddi_buf_trans_kbl_hs[9] = {
    { 0x000000a0, 0x00002016 },
    { 0x0000009b, 0x00005012 },
    { 0x00000088, 0x00007011 },
    { 0x000000c0, 0x80009010 },
    { 0x0000009b, 0x00002016 },
    { 0x00000088, 0x00005012 },
    { 0x000000c0, 0x80007011 },
    { 0x00000097, 0x00002016 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry dp_ddi_buf_trans_kbl_y[9] = {
    { 0x000000a1, 0x00001017 },
    { 0x00000088, 0x00005012 },
    { 0x000000cd, 0x80007011 },
    { 0x000000c0, 0x8000800f },
    { 0x0000009d, 0x00001017 },
    { 0x000000c0, 0x80005012 },
    { 0x000000c0, 0x80007011 },
    { 0x0000004c, 0x00001017 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry dp_ddi_buf_trans_kbl_u[9] = {
    { 0x000000a1, 0x0000201b },
    { 0x00000088, 0x00005012 },
    { 0x000000cd, 0x80007011 },
    { 0x000000c0, 0x80009010 },
    { 0x0000009d, 0x0000201b },
    { 0x000000c0, 0x80005012 },
    { 0x000000c0, 0x80007011 },
    { 0x0000004f, 0x00002016 },
    { 0x000000c0, 0x80005012 },
};

const ddi_buf_trans_entry edp_ddi_buf_trans_skl_hs[10] = {
    { 0x000000a8, 0x00000018 },
    { 0x000000a9, 0x00004013 },
    { 0x000000a2, 0x00007011 },
    { 0x0000009c, 0x00009010 },
    { 0x000000a9, 0x00000018 },
    { 0x000000a2, 0x00006013 },
    { 0x000000a6, 0x00007011 },
    { 0x000000ab, 0x00000018 },
    { 0x0000009f, 0x00007013 },
    { 0x000000df, 0x00000018 },
};

const ddi_buf_trans_entry edp_ddi_buf_trans_skl_y[10] = {
    { 0x000000a8, 0x00000018 },
    { 0x000000ab, 0x00004013 },
    { 0x000000a4, 0x00007011 },
    { 0x000000df, 0x00009010 },
    { 0x000000aa, 0x00000018 },
    { 0x000000a4, 0x00006013 },
    { 0x0000009d, 0x00007011 },
    { 0x000000a0, 0x00000018 },
    { 0x000000df, 0x00006012 },
    { 0x0000008a, 0x00000018 },
};

const ddi_buf_trans_entry edp_ddi_buf_trans_skl_u[10] = {
    { 0x000000a8, 0x00000018 },
    { 0x000000a9, 0x00004013 },
    { 0x000000a2, 0x00007011 },
    { 0x0000009c, 0x00009010 },
    { 0x000000a9, 0x00000018 },
    { 0x000000a2, 0x00006013 },
    { 0x000000a6, 0x00007011 },
    { 0x000000ab, 0x00002016 },
    { 0x0000009f, 0x00005013 },
    { 0x000000df, 0x00000018 },
};

void get_dp_ddi_buf_trans_entries(uint16_t device_id, const ddi_buf_trans_entry** entries,
                                  uint8_t* i_boost, unsigned* count) {
    if (is_skl(device_id)) {
        if (is_skl_u(device_id)) {
            *entries = dp_ddi_buf_trans_skl_u;
            *i_boost = 0x1;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_skl_u));
        } else if (is_skl_y(device_id)) {
            *entries = dp_ddi_buf_trans_skl_y;
            *i_boost = 0x3;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_skl_y));
        } else {
            *entries = dp_ddi_buf_trans_skl_hs;
            *i_boost = 0x1;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_skl_hs));
        }
    } else {
        ZX_DEBUG_ASSERT_MSG(is_kbl(device_id), "Expected kbl device");
        if (is_kbl_u(device_id)) {
            *entries = dp_ddi_buf_trans_kbl_u;
            *i_boost = 0x1;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_kbl_u));
        } else if (is_kbl_y(device_id)) {
            *entries = dp_ddi_buf_trans_kbl_y;
            *i_boost = 0x3;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_kbl_y));
        } else {
            *entries = dp_ddi_buf_trans_kbl_hs;
            *i_boost = 0x3;
            *count = static_cast<unsigned>(fbl::count_of(dp_ddi_buf_trans_kbl_hs));
        }
    }
}

void get_edp_ddi_buf_trans_entries(uint16_t device_id, const ddi_buf_trans_entry** entries,
                                   unsigned* count) {
    if (is_skl_u(device_id) || is_kbl_u(device_id)) {
        *entries = edp_ddi_buf_trans_skl_u;
        *count = static_cast<int>(fbl::count_of(edp_ddi_buf_trans_skl_u));
    } else if (is_skl_y(device_id) || is_kbl_y(device_id)) {
        *entries = edp_ddi_buf_trans_skl_y;
        *count = static_cast<int>(fbl::count_of(edp_ddi_buf_trans_skl_y));
    } else {
        *entries = edp_ddi_buf_trans_skl_hs;
        *count = static_cast<int>(fbl::count_of(edp_ddi_buf_trans_skl_hs));
    }
}

// Aux port functions

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
        mmio_space()->Write<uint32_t>(data_reg + offset, htobe32(*data));
    }

    auto status = ddi_regs.DdiAuxControl().ReadFrom(mmio_space());
    status.set_message_size(request.size);
    // Reset R/W Clear bits
    status.set_done(1);
    status.set_timeout(1);
    status.set_rcv_error(1);
    // The documentation says to not use setting 0 (400us), so use 1 (600us).
    status.set_timeout_timer_value(1);
    // TODO(ZX-1416): Support interrupts
    status.set_interrupt_on_done(1);
    // Send busy starts the transaction
    status.set_send_busy(1);
    status.WriteTo(mmio_space());

    // Poll for the reply message.
    const int kNumTries = 10000;
    for (int tries = 0; tries < kNumTries; ++tries) {
        auto status = ddi_regs.DdiAuxControl().ReadFrom(mmio_space());
        if (!status.send_busy()) {
            if (status.timeout()) {
                *timeout_result = true;
                return false;
            }
            if (status.rcv_error()) {
                zxlogf(ERROR, "DP aux: rcv error\n");
                return false;
            }
            if (!status.done()) {
                continue;
            }

            reply->size = status.message_size();
            if (!reply->size || reply->size > DpAuxMessage::kMaxTotalSize) {
                zxlogf(ERROR, "DP aux: Invalid reply size %d\n", reply->size);
                return false;
            }
            // Read the reply message from the hardware.
            for (uint32_t offset = 0; offset < reply->size; offset += 4) {
                // For some reason intel made these data registers big endian...
                *reinterpret_cast<uint32_t*>(reply->data + offset) =
                        be32toh(mmio_space()->Read<uint32_t>(data_reg + offset));
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

bool DpDisplay::ReadEdid(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) {
    // Ignore failures setting the segment if segment == 0, since it could be the case
    // that the display doesn't support segments.
    return (DpAuxWrite(DP_REQUEST_I2C_WRITE, kDdcSegmentI2cAddress, &segment, 1) || segment == 0)
            && DpAuxWrite(DP_REQUEST_I2C_WRITE, kDdcDataI2cAddress, &offset, 1)
            && DpAuxRead(DP_REQUEST_I2C_READ, kDdcDataI2cAddress, buf, len);
}

bool DpDisplay::DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size) {
    return DpAuxRead(DP_REQUEST_NATIVE_READ, addr, buf, size);
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
bool DpDisplay::DpcdReadPairedRegs(hwreg::RegisterBase<T, typename T::ValueType>* regs) {
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
    // doc, the max voltage swing is 2/3 for DP/eDP and the max (voltage swing + pre-emphasis) is
    // 3. According to the v1.1a of the DP docs, if v + pe is too large then v should be reduced
    // to the highest supported value for the pe level (section 3.5.1.3)
    static constexpr uint32_t kMaxVPlusPe = 3;
    uint8_t max_v = controller()->igd_opregion().IsLowVoltageEdp(ddi()) ? 3 : 2;
    if (v + pe > kMaxVPlusPe) {
        v = static_cast<uint8_t>(kMaxVPlusPe - pe);
    }
    if (v > max_v) {
        v = max_v;
    }

    for (unsigned i = 0; i < dp_lane_count_; i++) {
        voltage_change |= (training[i].voltage_swing_set() != v);
        training[i].set_voltage_swing_set(v);
        training[i].set_max_swing_reached(v == max_v);
        training[i].set_pre_emphasis_set(pe);
        training[i].set_max_pre_emphasis_set(pe + v == kMaxVPlusPe);
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
    buf_ctl.set_dp_vswing_emp_sel(level);
    buf_ctl.WriteTo(mmio_space());

    return voltage_change;
}

bool DpDisplay::LinkTrainingSetup() {
    registers::DdiRegs ddi_regs(ddi());

    // TODO(ZX-1416): set SET_POWER dpcd field (0x600)

    uint8_t max_lc_byte;
    if (!DpcdRead(dpcd::DPCD_MAX_LANE_COUNT, &max_lc_byte, 1)) {
        zxlogf(ERROR, "Failed to read lane count\n");
        return false;
    }
    dpcd::LaneCount max_lc;
    max_lc.set_reg_value(max_lc_byte);
    dp_lane_count_ = max_lc.lane_count_set();

    // Tell the source device to emit the training pattern.
    auto dp_tp = ddi_regs.DdiDpTransportControl().ReadFrom(mmio_space());
    dp_tp.set_transport_enable(1);
    dp_tp.set_transport_mode_select(0);
    dp_tp.set_enhanced_framing_enable(max_lc.enhanced_frame_enabled());
    dp_tp.set_dp_link_training_pattern(dp_tp.kTrainingPattern1);
    dp_tp.WriteTo(mmio_space());

    // Configure ddi voltage swing
    // TODO(ZX-1416): Read the VBT to handle unique motherboard configs for kaby lake
    unsigned count;
    uint8_t i_boost;
    const ddi_buf_trans_entry* entries;
    if (controller()->igd_opregion().IsLowVoltageEdp(ddi())) {
        i_boost = 0;
        get_edp_ddi_buf_trans_entries(controller()->device_id(), &entries, &count);
    } else {
        get_dp_ddi_buf_trans_entries(controller()->device_id(), &entries, &i_boost, &count);
    }
    uint8_t i_boost_override = controller()->igd_opregion().GetIBoost(ddi());

    for (unsigned i = 0; i < count; i++) {
        auto ddi_buf_trans_high = ddi_regs.DdiBufTransHi(i).ReadFrom(mmio_space());
        auto ddi_buf_trans_low = ddi_regs.DdiBufTransLo(i).ReadFrom(mmio_space());
        ddi_buf_trans_high.set_reg_value(entries[i].high_dword);
        ddi_buf_trans_low.set_reg_value(entries[i].low_dword);
        if (i_boost_override) {
            ddi_buf_trans_low.set_balance_leg_enable(1);
        }
        ddi_buf_trans_high.WriteTo(mmio_space());
        ddi_buf_trans_low.WriteTo(mmio_space());
    }
    auto disio_cr_tx_bmu = registers::DisplayIoCtrlRegTxBmu::Get().ReadFrom(mmio_space());
    disio_cr_tx_bmu.set_disable_balance_leg(!i_boost && !i_boost_override);
    disio_cr_tx_bmu.tx_balance_leg_select(ddi()).set(i_boost_override ? i_boost_override : i_boost);
    disio_cr_tx_bmu.WriteTo(mmio_space());

    // Enable and wait for DDI_BUF_CTL
    auto buf_ctl = ddi_regs.DdiBufControl().ReadFrom(mmio_space());
    buf_ctl.set_ddi_buffer_enable(1);
    buf_ctl.set_dp_vswing_emp_sel(0);
    buf_ctl.set_dp_port_width_selection(dp_lane_count_ - 1);
    buf_ctl.WriteTo(mmio_space());
    zx_nanosleep(zx_deadline_after(ZX_USEC(518)));

    // Configure the bandwidth and lane count settings
    dpcd::LinkBw bw_setting;
    bw_setting.set_link_bw_set(bw_setting.k2700Mbps); // kLinkRateMhz
    dpcd::LaneCount lc_setting;
    lc_setting.set_lane_count_set(dp_lane_count_);
    lc_setting.set_enhanced_frame_enabled(max_lc.enhanced_frame_enabled());
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
    tp_set->set_training_pattern_set(tp_set->kTrainingPattern1);
    tp_set->set_scrambling_disable(1);

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
            if (lanes[i].max_swing_reached()) {
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

    dp_tp.set_dp_link_training_pattern(dp_tp.kTrainingPattern2);
    dp_tp.WriteTo(mmio_space());

    tp_set->set_training_pattern_set(tp_set->kTrainingPattern2);
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

    dp_tp.set_dp_link_training_pattern(dp_tp.kSendPixelData);
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
    // The exact values of N and M shouldn't matter too much.  N and M can be
    // up to 24 bits, and larger values will tend to represent the ratio more
    // accurately. However, large values of N (e.g. 1 << 23) cause some monitors
    // to inexplicably fail. Pick a relatively arbitrary value for N that works
    // well in practice.
    *n_out = 1 << 20;
    *m_out = static_cast<uint32_t>(static_cast<uint64_t>(x) * *n_out / y);

}

} // namespace

namespace i915 {

DpDisplay::DpDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe)
        : DisplayDevice(controller, ddi, pipe) { }

bool DpDisplay::Init(zx_display_info* info) {
    if (!ResetPipe() || !ResetDdi()) {
        return false;
    }

    edid::Edid edid(this);
    edid::timing_params_t timing;
    if (!edid.Init() || !edid.GetPreferredTiming(&timing)) {
        return false;
    }
    zxlogf(TRACE, "Found a displayport monitor\n");

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
    dpll_enable.set_enable_dpll(1);
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
            ::Get().ReadFrom(mmio_space()).ddi_io_power_state(ddi()).get(), 20)) {
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
    clock_select.set_trans_clock_select(ddi() + 1);
    clock_select.WriteTo(mmio_space());

    // Pixel clock rate: The rate at which pixels are sent, in pixels per
    // second (Hz), divided by 10000.
    uint32_t pixel_clock_rate = timing.pixel_freq_10khz;

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
    data_m_reg.set_tu_or_vcpayload_size(63); // Size - 1, default TU size is 64
    data_m_reg.set_data_m_value(data_m);
    data_m_reg.WriteTo(mmio_space());

    auto data_n_reg = trans.DataN().FromValue(0);
    data_n_reg.set_data_n_value(data_n);
    data_n_reg.WriteTo(mmio_space());

    auto link_m_reg = trans.LinkM().FromValue(0);
    link_m_reg.set_link_m_value(link_m);
    link_m_reg.WriteTo(mmio_space());

    auto link_n_reg = trans.LinkN().FromValue(0);
    link_n_reg.set_link_n_value(link_n);
    link_n_reg.WriteTo(mmio_space());

    // Configure the rest of the transcoder
    uint32_t h_active = timing.horizontal_addressable - 1;
    uint32_t h_sync_start = h_active + timing.horizontal_front_porch;
    uint32_t h_sync_end = h_sync_start + timing.horizontal_sync_pulse;
    uint32_t h_total = h_sync_end + timing.horizontal_back_porch;

    uint32_t v_active = timing.vertical_addressable - 1;
    uint32_t v_sync_start = v_active + timing.vertical_front_porch;
    uint32_t v_sync_end = v_sync_start + timing.vertical_sync_pulse;
    uint32_t v_total = v_sync_end + timing.vertical_back_porch;

    auto h_total_reg = trans.HTotal().FromValue(0);
    h_total_reg.set_count_total(h_total);
    h_total_reg.set_count_active(h_active);
    h_total_reg.WriteTo(mmio_space());
    auto v_total_reg = trans.VTotal().FromValue(0);
    v_total_reg.set_count_total(v_total);
    v_total_reg.set_count_active(v_active);
    v_total_reg.WriteTo(mmio_space());

    auto h_sync_reg = trans.HSync().FromValue(0);
    h_sync_reg.set_sync_start(h_sync_start);
    h_sync_reg.set_sync_end(h_sync_end);
    h_sync_reg.WriteTo(mmio_space());
    auto v_sync_reg = trans.VSync().FromValue(0);
    v_sync_reg.set_sync_start(v_sync_start);
    v_sync_reg.set_sync_end(v_sync_end);
    v_sync_reg.WriteTo(mmio_space());

    // The Intel docs say that H/VBlank should be programmed with the same H/VTotal
    trans.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(mmio_space());
    trans.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(mmio_space());

    auto msa_misc = trans.MsaMisc().FromValue(0);
    msa_misc.set_sync_clock(1);
    msa_misc.set_bits_per_color(msa_misc.k8Bbc); // kPixelFormat
    msa_misc.set_color_format(msa_misc.kRgb); // kPixelFormat
    msa_misc.WriteTo(mmio_space());

    auto ddi_func = trans.DdiFuncControl().ReadFrom(mmio_space());
    ddi_func.set_trans_ddi_function_enable(1);
    ddi_func.set_ddi_select(ddi());
    ddi_func.set_trans_ddi_mode_select(ddi_func.kModeDisplayPortSst);
    ddi_func.set_bits_per_color(ddi_func.k8bbc); // kPixelFormat
    ddi_func.set_sync_polarity(timing.vertical_sync_polarity << 1 | timing.horizontal_sync_polarity);
    ddi_func.set_port_sync_mode_enable(0);
    ddi_func.set_dp_vc_payload_allocate(0);
    ddi_func.set_dp_port_width_selection(dp_lane_count_ - 1);
    ddi_func.WriteTo(mmio_space());

    auto trans_conf = trans.Conf().FromValue(0);
    trans_conf.set_transcoder_enable(1);
    trans_conf.set_interlaced_mode(timing.interlaced);
    trans_conf.WriteTo(mmio_space());

    // Configure the pipe
    registers::PipeRegs pipe_regs(pipe());

    auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
    pipe_size.set_horizontal_source_size(h_active);
    pipe_size.set_vertical_source_size(v_active);
    pipe_size.WriteTo(mmio_space());


    auto plane_control = pipe_regs.PlaneControl().FromValue(0);
    plane_control.set_plane_enable(1);
    plane_control.set_source_pixel_format(plane_control.kFormatRgb8888); // kPixelFormat
    plane_control.set_tiled_surface(plane_control.kLinear);
    plane_control.WriteTo(mmio_space());

    auto plane_size = pipe_regs.PlaneSurfaceSize().FromValue(0);
    plane_size.set_width_minus_1(h_active);
    plane_size.set_height_minus_1(v_active);
    plane_size.WriteTo(mmio_space());

    info->width = timing.horizontal_addressable;
    info->height = timing.vertical_addressable;
    info->stride = ROUNDUP(info->width, registers::PlaneSurfaceStride::kLinearStrideChunkSize);
    info->format = kPixelFormat;
    info->pixelsize = ZX_PIXEL_FORMAT_BYTES(info->format);

    return true;
}

} // namespace i915
