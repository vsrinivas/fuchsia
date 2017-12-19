// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "display-device.h"
#include "dpcd.h"
#include "edid.h"

namespace i915 {

class DpAuxMessage;

class DpDisplay : public DisplayDevice, private edid::EdidSource {
public:
    DpDisplay(Controller* controller, uint16_t device_id, registers::Ddi ddi, registers::Pipe pipe);

private:
    bool Init(zx_display_info* info) final;
    bool ReadEdid(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) final;

    bool DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size);
    bool DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                        uint32_t* size_out);
    bool DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, uint32_t size);
    bool SendDpAuxMsg(const DpAuxMessage& request, DpAuxMessage* reply, bool* timeout_result);
    bool SendDpAuxMsgWithRetry(const DpAuxMessage& request, DpAuxMessage* reply);

    bool DpcdRead(uint32_t addr, uint8_t* buf, uint32_t size);
    bool DpcdWrite(uint32_t addr, const uint8_t* buf, uint32_t size);

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

    uint32_t dp_lane_count_;
};

} // namespace i915
