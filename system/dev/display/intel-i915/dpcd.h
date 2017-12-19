// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>

// DisplayPort Configuration Data
namespace dpcd {

// DPCD register numbers.
enum {
    DPCD_MAX_LANE_COUNT = 0x2,
    DPCD_LINK_BW_SET = 0x100,
    DPCD_COUNT_SET = 0x101,
    DPCD_TRAINING_PATTERN_SET = 0x102,
    DPCD_TRAINING_LANE0_SET = 0x103,
    DPCD_TRAINING_LANE1_SET = 0x104,
    DPCD_TRAINING_LANE2_SET = 0x105,
    DPCD_TRAINING_LANE3_SET = 0x106,
    DPCD_LANE0_1_STATUS = 0x202,
    DPCD_ADJUST_REQUEST_LANE0_1 = 0x206,
};

// DPCD register: MAX_LINK_RATE and LINK_BW_SET
class LinkBw : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_FIELD(7, 0, link_bw_set);
    static constexpr int k1620Mbps = 0x06;
    static constexpr int k2700Mbps = 0x0A;
};

// DPCD register: MAX_LANE_COUNT and LANE_COUNT_SET
class LaneCount : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_BIT(7, enhanced_frame_enabled);
    DEF_FIELD(4, 0, lane_count_set);
};

// DPCD register: TRAINING_PATTERN_SET
class TrainingPatternSet : public hwreg::RegisterBase<uint32_t> {
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
class TrainingLaneSet : public hwreg::RegisterBase<uint32_t> {
public:
    DEF_FIELD(1, 0, voltage_swing_set);
    DEF_BIT(2, max_swing_reached);
    DEF_FIELD(4, 3, pre_emphasis_set);
    DEF_BIT(5, max_pre_emphasis_set);
};

// DPCD register: LANEX_Y_STATUS
class LaneStatus : public hwreg::RegisterBase<uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> lane_cr_done(int lane) {
        int bit = 4 * (lane % 2);
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> lane_channel_eq_done(int lane) {
        int bit = 4 * (lane % 2) + 1;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }

    hwreg::BitfieldRef<uint32_t> lane_symbol_locked(int lane) {
        int bit = 4 * (lane % 2) + 2;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
    }
};

// DPCD register: ADJUST_REQUEST_LANEX_Y
class AdjustRequestLane : public hwreg::RegisterBase<uint32_t> {
public:
    hwreg::BitfieldRef<uint32_t> voltage_swing(int lane) {
        int bit = 4 * (lane % 2);
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }

    hwreg::BitfieldRef<uint32_t> pre_emphasis(int lane) {
        int bit = 4 * (lane % 2) + 2;
        return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
    }
};

} // dpcd
