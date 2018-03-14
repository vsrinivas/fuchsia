// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>

// DisplayPort Configuration Data
namespace dpcd {

// DPCD register numbers.
enum {
    DPCD_CAP_START = 0x0,
    DPCD_REV = 0x0,
    DPCD_MAX_LINK_RATE = 0x1,
    DPCD_MAX_LANE_COUNT = 0x2,
    DPCD_DOWN_STREAM_PORT_PRESENT = 0x5,
    DPCD_DOWN_STREAM_PORT_COUNT = 0x7,
    DPCD_EDP_CONFIG = 0xd,
    DPCD_SUPPORTED_LINK_RATE_START = 0x10,
    DPCD_SUPPORTED_LINK_RATE_END = 0x1f,
    DPCD_LINK_BW_SET = 0x100,
    DPCD_COUNT_SET = 0x101,
    DPCD_TRAINING_PATTERN_SET = 0x102,
    DPCD_TRAINING_LANE0_SET = 0x103,
    DPCD_TRAINING_LANE1_SET = 0x104,
    DPCD_TRAINING_LANE2_SET = 0x105,
    DPCD_TRAINING_LANE3_SET = 0x106,
    DPCD_LINK_RATE_SET = 0x115,
    DPCD_SINK_COUNT = 0x200,
    DPCD_LANE0_1_STATUS = 0x202,
    DPCD_LANE_ALIGN_STATUS_UPDATED = 0x204,
    DPCD_ADJUST_REQUEST_LANE0_1 = 0x206,
    DPCD_SET_POWER = 0x600,
    DPCD_EDP_CAP_START = 0x700,
    DPCD_EDP_GENERAL_CAP1 = 0x701,
    DPCD_EDP_BACKLIGHT_CAP = 0x702,
    DPCD_EDP_DISPLAY_CTRL = 0x720,
    DPCD_EDP_BACKLIGHT_MODE_SET = 0x721,
    DPCD_EDP_BACKLIGHT_BRIGHTNESS_MSB = 0x722,
    DPCD_EDP_BACKLIGHT_BRIGHTNESS_LSB = 0x723,
};

// DPCD register: MAX_LINK_RATE and LINK_BW_SET
class LinkBw : public hwreg::RegisterBase<LinkBw, uint8_t> {
public:
    DEF_FIELD(7, 0, link_bw);
    static constexpr int k1620Mbps = 0x06;
    static constexpr int k2700Mbps = 0x0A;
    static constexpr int k5400Mbps = 0x14;
    static constexpr int k8100Mbps = 0x1e;
};

// DPCD register: MAX_LANE_COUNT and LANE_COUNT_SET
class LaneCount : public hwreg::RegisterBase<LaneCount, uint8_t> {
public:
    DEF_BIT(7, enhanced_frame_enabled);
    DEF_FIELD(4, 0, lane_count_set);
};

// DPCD register: TRAINING_PATTERN_SET
class TrainingPatternSet : public hwreg::RegisterBase<TrainingPatternSet, uint8_t> {
public:
    DEF_FIELD(1, 0, training_pattern_set);
    static constexpr int kNotTraining = 0;
    static constexpr int kTrainingPattern1 = 1;
    static constexpr int kTrainingPattern2 = 2;

    DEF_FIELD(3, 2, link_qual_pattern_set);
    DEF_BIT(4, recovered_clock_out_enable);
    DEF_BIT(5, scrambling_disable);
};

// DPCD register: TRAINING_LANEX_SET
class TrainingLaneSet : public hwreg::RegisterBase<TrainingLaneSet, uint8_t> {
public:
    DEF_FIELD(1, 0, voltage_swing_set);
    DEF_BIT(2, max_swing_reached);
    DEF_FIELD(4, 3, pre_emphasis_set);
    DEF_BIT(5, max_pre_emphasis_set);
};

// DPCD register: LANEX_Y_STATUS
class LaneStatus : public hwreg::RegisterBase<LaneStatus, uint8_t> {
public:
    hwreg::BitfieldRef<uint8_t> lane_cr_done(int lane) {
        int bit = 4 * (lane % 2);
        return hwreg::BitfieldRef<uint8_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint8_t> lane_channel_eq_done(int lane) {
        int bit = 4 * (lane % 2) + 1;
        return hwreg::BitfieldRef<uint8_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint8_t> lane_symbol_locked(int lane) {
        int bit = 4 * (lane % 2) + 2;
        return hwreg::BitfieldRef<uint8_t>(reg_value_ptr(), bit, bit);
    }
};

// DPCD register: ADJUST_REQUEST_LANEX_Y
class AdjustRequestLane : public hwreg::RegisterBase<AdjustRequestLane, uint8_t> {
public:
    hwreg::BitfieldRef<uint8_t> voltage_swing(int lane) {
        int bit = 4 * (lane % 2);
        return hwreg::BitfieldRef<uint8_t>(reg_value_ptr(), bit + 1, bit);
    }

    hwreg::BitfieldRef<uint8_t> pre_emphasis(int lane) {
        int bit = 4 * (lane % 2) + 2;
        return hwreg::BitfieldRef<uint8_t>(reg_value_ptr(), bit + 1, bit);
    }
};

// DPCD register: eDP_CONFIGURATION_CAP
class EdpConfigCap : public hwreg::RegisterBase<EdpConfigCap, uint8_t> {
public:
    DEF_BIT(0, alt_scrambler_reset_capable);
    DEF_BIT(3, dpcd_display_ctrl_capable);
};

// DPCD register: EDP_GENERAL_CAPABILITY_1
class EdpGeneralCap1 : public hwreg::RegisterBase<EdpGeneralCap1, uint8_t> {
public:
    DEF_BIT(0, tcon_backlight_adjustment_cap);
    DEF_BIT(1, backlight_pin_enable_cap);
    DEF_BIT(2, backlight_aux_enable_cap);
    DEF_BIT(3, panel_self_test_pin_enable_cap);
    DEF_BIT(4, panel_self_test_aux_enable_cap);
    DEF_BIT(5, frc_enable_cap);
    DEF_BIT(6, color_engine_cap);
    DEF_BIT(7, set_power_cap);
};

// DPCD registers: EDP_BACKLIGHT_ADJUSTMENT_CAPABILITIES
class EdpBacklightCap : public hwreg::RegisterBase<EdpBacklightCap, uint8_t> {
public:
    DEF_BIT(0, brightness_pwm_pin_cap);
    DEF_BIT(1, brightness_aux_set_cap);
    DEF_BIT(2, brightness_byte_count);
    DEF_BIT(3, aux_pwm_product_cap);
    DEF_BIT(4, freq_pwm_pin_passthru_cap);
    DEF_BIT(5, freq_aux_set_cap);
    DEF_BIT(6, dynamic_backlight_cap);
    DEF_BIT(7, vblank_backlight_update_cap);
};

// DPCD registers: EDP_BACKLIGHT_MODE_SET
class EdpBacklightModeSet : public hwreg::RegisterBase<EdpBacklightModeSet, uint8_t> {
public:
    DEF_FIELD(1, 0, brightness_ctrl_mode);
    static constexpr uint8_t kPwmPin = 0;
    static constexpr uint8_t kPresetBrightness = 1;
    static constexpr uint8_t kAux = 2;
    static constexpr uint8_t kAuxTimesPwmPin = 3;
    DEF_BIT(2, freq_pwm_pin_passthru_enable);
    DEF_BIT(3, freq_aux_set_enable);
    DEF_BIT(4, dynamic_backlight_enable);
    DEF_BIT(5, regional_backlight_enable);
    DEF_BIT(6, update_regional_backlight);
};

// DPCD registers: EDP_DISPLAY_CONTROL
class EdpDisplayCtrl : public hwreg::RegisterBase<EdpDisplayCtrl, uint8_t> {
public:
    DEF_BIT(0, backlight_enable);
    DEF_BIT(1, black_video_enable);
    DEF_BIT(2, frame_rate_control_enable);
    DEF_BIT(3, color_engine_enable);
    DEF_BIT(7, vblank_backlight_update_enable);
};

// DPCD registers:: SET_POWER
class SetPower : public hwreg::RegisterBase<SetPower, uint8_t> {
public:
    DEF_FIELD(2, 0, set_power_state);
    static constexpr uint8_t kOn = 1;
    static constexpr uint8_t kOff = 2;
    static constexpr uint8_t kOffWithAux = 5;
    DEF_RSVDZ_FIELD(4, 3);
    DEF_BIT(5, set_dn_device_dp_pwr_5v);
    DEF_BIT(6, set_dn_device_dp_pwr_12v);
    DEF_BIT(7, set_dn_device_dp_pwr_18v);
};

// DPCD registers:: LINK_RATE_SET
class LinkRateSet : public hwreg::RegisterBase<LinkRateSet, uint8_t> {
public:
    DEF_FIELD(2, 0, link_rate_idx);
    DEF_BIT(3, tx_gtc_cap);
    DEF_BIT(4, tx_gtc_slave_cap);
    DEF_RSVDZ_FIELD(7, 5);
};

// DPCD registers:: DOWN_STREAM_PORT_PRESENT
class DownStreamPortPresent : public hwreg::RegisterBase<DownStreamPortPresent, uint8_t> {
public:
    DEF_BIT(0, is_branch);
    DEF_FIELD(2, 1, type);
    static constexpr uint8_t kDp = 0;
    static constexpr uint8_t kAnalog = 1;
    static constexpr uint8_t kDviHdmiDpPlus = 2;
    static constexpr uint8_t kOther = 3;
    DEF_BIT(3, format_conversion);
    DEF_BIT(4, detailed_cap_info_available);
};

// DPCD registers:: DOWN_STREAM_PORT_COUNT
class DownStreamPortCount : public hwreg::RegisterBase<DownStreamPortCount, uint8_t> {
public:
    DEF_FIELD(3, 0, count);
    DEF_BIT(6, msa_timing_par_ignored);
    DEF_BIT(7, oui_supported);
};

// DPCD registers:: SINK_COUNT
class SinkCount : public hwreg::RegisterBase<SinkCount, uint8_t> {
public:
    DEF_FIELD(5, 0, count_lo);
    DEF_BIT(6, cp_ready);
    DEF_BIT(7, count_hi);

    uint8_t count() const {
        return static_cast<uint8_t>(count_lo() | (count_hi() << 6));
    }
};

// DPCD registers:: LANE_ALIGN_STATUS_UPDATED
class LaneAlignStatusUpdate : public hwreg::RegisterBase<LaneAlignStatusUpdate, uint8_t> {
public:
    DEF_BIT(0, interlane_align_done);
    DEF_BIT(1, post_lt_adj_req_in_progress);
    DEF_BIT(6, downstream_port_status_changed);
    DEF_BIT(7, link_status_updated);
};

} // dpcd
