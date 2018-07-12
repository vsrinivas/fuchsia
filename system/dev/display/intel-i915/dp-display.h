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
    DpDisplay(Controller* controller, uint64_t id, registers::Ddi ddi);

private:
    bool QueryDevice(edid::Edid* edid) final;
    bool ConfigureDdi() final;
    bool PipeConfigPreamble(registers::Pipe pipe, registers::Trans trans) final;
    bool PipeConfigEpilogue(registers::Pipe pipe, registers::Trans trans) final;

    bool DdcRead(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) final;

    bool CheckDisplayLimits(const display_config_t* config) final;

    zx_status_t DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, size_t size);
    zx_status_t DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                               size_t* size_out);
    zx_status_t DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, size_t size);
    zx_status_t SendDpAuxMsg(const DpAuxMessage& request, DpAuxMessage* reply);
    zx_status_t SendDpAuxMsgWithRetry(const DpAuxMessage& request, DpAuxMessage* reply);

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

    bool IsBacklightOn();
    // Sets the backlight brightness with |val| as a coefficient on the maximum
    // brightness. |val| must be in [0, 1]. If the panel has a minimum fractional
    // brightness, then |val| will be clamped to [min, 1].
    bool SetBacklightBrightness(double val);

    // Gets the backlight brightness as a coefficient on the maximum brightness,
    // between the minimum brightness and 1.
    double GetBacklightBrightness();

    bool HandleHotplug(bool long_pulse) override;
    bool HasBacklight() override;
    void SetBacklightState(bool power, uint8_t brightness) override;
    void GetBacklightState(bool* power, uint8_t* brightness) override;

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
