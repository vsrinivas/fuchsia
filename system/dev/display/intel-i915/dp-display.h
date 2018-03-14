// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display-device.h"
#include "dpcd.h"

namespace i915 {

class DpAuxMessage;

class DpDisplay : public DisplayDevice, private edid::EdidDdcSource {
public:
    DpDisplay(Controller* controller, registers::Ddi ddi, registers::Pipe pipe);

private:
    bool QueryDevice(edid::Edid* edid, zx_display_info_t* info) final;
    bool DefaultModeset() final;
    bool DdcRead(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) final;

    bool DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, size_t size);
    bool DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                        size_t* size_out);
    bool DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, size_t size);
    bool SendDpAuxMsg(const DpAuxMessage& request, DpAuxMessage* reply, bool* timeout_result);
    bool SendDpAuxMsgWithRetry(const DpAuxMessage& request, DpAuxMessage* reply);

    bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size);
    bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size);

    bool DpcdRequestLinkTraining(const dpcd::TrainingPatternSet& tp_set,
                                 const dpcd::TrainingLaneSet lanes[]);
    template<uint32_t addr, typename T> bool DpcdReadPairedRegs(
            hwreg::RegisterBase<T, typename T::ValueType>* status);
    bool DpcdHandleAdjustRequest(dpcd::TrainingLaneSet* training, dpcd::AdjustRequestLane* adjust);
    bool DoLinkTraining();
    bool LinkTrainingSetup();
    // For locking Clock Recovery Circuit of the DisplayPort receiver
    bool LinkTrainingStage1(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);
    // For optimizing equalization, determining symbol  boundary, and achieving inter-lane alignment
    bool LinkTrainingStage2(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);

    bool SetBacklightOn(bool on);
    // Sets the backlight brightness with |val| as a coefficient on the maximum
    // brightness. |val| must be in [0, 1]. If the panel has a minimum fractional
    // brightness, then |val| will be clamped to [min, 1].
    bool SetBacklightBrightness(double val);

    bool HandleHotplug(bool long_pulse) override;

    uint8_t dp_lane_count_;
    uint32_t dp_link_rate_mhz_;
    uint8_t dp_link_rate_idx_plus1_;
    bool dp_enhanced_framing_enabled_;

    uint8_t dpcd_capabilities_[16];
    uint8_t dpcd_edp_capabilities_[5];
    bool backlight_aux_brightness_;
    bool backlight_aux_power_;

    // The backlight brightness coefficient, in the range [min brightness, 1].
    double backlight_brightness_ = 1.0f;
};

} // namespace i915
