// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "codec_state.h"

namespace audio {
namespace intel_hda {

class IntelHDACodec;

class CodecStateFetcher {
public:
    using FinishedFn = zx_status_t (InitialCodecStateFetcher::*)();

    InitialCodecStateFetcher(IntelHDACodec& codec);

    bool IsFinished() final {
        // We are finished when we have received all responses to our last command
        // list and have no function groups left to process.
        return (rx_ndx_ >= cmd_count_) && (fn_group_iter_ >= codec_.fn_group_count_);
    }

    // Accessors used by non-member result parser functions
    CodecState& get_codec() const { return codec_; }

    FunctionGroupStatePtr& get_fn_group_ptr() const {
        ZX_DEBUG_ASSERT(codec_.fn_groups_ != nullptr);
        ZX_DEBUG_ASSERT(fn_group_iter_ < codec_.fn_group_count_);
        return codec_.fn_groups_[fn_group_iter_];
    }

    AudioFunctionGroupState& get_afg() const {
        auto& ptr = get_fn_group_ptr();
        ZX_DEBUG_ASSERT(ptr != nullptr);
        ZX_DEBUG_ASSERT(ptr->type_ == FunctionGroupState::Type::AUDIO);
        return *(static_cast<AudioFunctionGroupState*>(ptr.get()));
    }

    AudioWidgetStatePtr& get_widget_ptr() const {
        auto& afg = get_afg();
        ZX_DEBUG_ASSERT(afg.widgets_ != nullptr);
        ZX_DEBUG_ASSERT(widget_iter_ < afg.widget_count_);
        return afg.widgets_[widget_iter_];
    }

    AudioWidgetState& get_widget() const {
        auto& ptr = get_widget_ptr();
        ZX_DEBUG_ASSERT(ptr != nullptr);
        return *ptr;
    }

    uint16_t get_nid() const { return nid_; }

private:
    zx_status_t FinishedCodecRoot();
    zx_status_t FinishedFunctionGroup();
    zx_status_t FinishedFunctionGroupType();
    zx_status_t FinishedAFGProperties();
    zx_status_t FinishedAudioWidget();
    zx_status_t FinishedAudioWidgetType();
    zx_status_t FinishedAudioWidgetCaps();
    zx_status_t FinishedConnList();

    void SetupCmdList(const CommandListEntry* cmds,
                      size_t                  cmd_count,
                      FinishedFn              finished,
                      uint16_t                nid) {
        cmds_      = cmds;
        cmd_count_ = cmd_count;
        tx_ndx_    = 0;
        rx_ndx_    = 0;
        nid_       = nid;
        send_cmd_  = cmd_count ? &InitialCodecStateFetcher::CommandListTX : nullptr;
        proc_resp_ = &InitialCodecStateFetcher::CommandListRX;
        finished_  = finished;
    }

    void SetupConnListFetch() {
        cmd_count_ = 0;
        tx_ndx_    = 0;
        rx_ndx_    = 0;
        send_cmd_  = &InitialCodecStateFetcher::ConnListTX;
        proc_resp_ = &InitialCodecStateFetcher::ConnListRX;
        finished_  = &InitialCodecStateFetcher::FinishedAudioWidget;
    }

    IntelHDACodec&          codec_;
    const CommandListEntry* cmds_      = nullptr;
    size_t                  cmd_count_ = 0;
    size_t                  tx_ndx_    = 0;
    size_t                  rx_ndx_    = 0;
    uint16_t                nid_       = 0;
    SendCommandsFn          send_cmd_  = nullptr;
    ProcResponseFn          proc_resp_ = nullptr;
    FinishedFn              finished_  = nullptr;

    uint32_t                fn_group_iter_  = -1;
    uint32_t                widget_iter_    = -1;
};

}  // namespace audio
}  // namespace intel_hda
