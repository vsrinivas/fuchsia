// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>

#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/codec-caps.h>

#include "utils.h"

namespace audio {
namespace intel_hda {
namespace codecs {

#define DECLARE_THUNK(_name) \
    zx_status_t _name(const Command& cmd, const CodecResponse& resp) __TA_REQUIRES(obj_lock());

class RealtekStream : public IntelHDAStreamBase {
public:
    RealtekStream(const StreamProperties& props)
        : IntelHDAStreamBase(props.stream_id, props.is_input),
          props_(props) { }

protected:
    friend class fbl::RefPtr<RealtekStream>;

    virtual ~RealtekStream() { }

    // IntelHDAStreamBase implementation
    zx_status_t OnActivateLocked()    __TA_REQUIRES(obj_lock()) final;
    void        OnDeactivateLocked()  __TA_REQUIRES(obj_lock()) final;
    void        OnChannelDeactivateLocked(const dispatcher::Channel& channel)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnDMAAssignedLocked() __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnSolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t OnUnsolicitedResponseLocked(const CodecResponse& resp)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt)
        __TA_REQUIRES(obj_lock()) final;
    zx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        __TA_REQUIRES(obj_lock()) final;
    void OnGetGainLocked(audio_proto::GetGainResp* out_resp) __TA_REQUIRES(obj_lock()) final;
    void OnSetGainLocked(const audio_proto::SetGainReq& req,
                         audio_proto::SetGainResp* out_resp) __TA_REQUIRES(obj_lock()) final;
    void OnPlugDetectLocked(dispatcher::Channel* response_channel,
                            const audio_proto::PlugDetectReq& req,
                            audio_proto::PlugDetectResp* out_resp) __TA_REQUIRES(obj_lock()) final;

private:
    struct Command {
        using Thunk = zx_status_t (RealtekStream::*)(const Command& cmd,
                                                     const CodecResponse& resp);
        const uint16_t  nid;
        const CodecVerb verb;
        const Thunk     thunk = nullptr;
    };

    // Capabilities common to both converters and pin complexes.
    struct CommonCaps {
        AudioWidgetCaps widget_caps;
        AmpCaps         amp_caps;
        bool            has_amp   = false;
        float           max_gain  = 0.0;
        float           min_gain  = 0.0;
        float           gain_step = 0.0;
    };

    // Capabilities for converters
    struct ConverterCaps : public CommonCaps {
        SampleCaps      sample_caps;
    };

    // Capabilities for pin complexes
    struct PinComplexCaps : public CommonCaps {
        ConfigDefaults  cfg_defaults;
        PinCaps         pin_caps;
        bool            async_plug_det = false;
        uint8_t         unsol_tag;
    };

    // Declare a slab allocator for PendingCommands.  Note; it needs to be made
    // our friend in order to see the definition of the PendingCommand private
    // inner class.
    class  PendingCommand;
    using  PCAT = fbl::StaticSlabAllocatorTraits<fbl::unique_ptr<PendingCommand>, 4096>;
    using  PendingCommandAllocator = fbl::SlabAllocator<PCAT>;
    friend PendingCommandAllocator;

    class PendingCommand : public fbl::DoublyLinkedListable<fbl::unique_ptr<PendingCommand>>,
                           public fbl::SlabAllocated<PCAT> {
    public:
        const Command& cmd() const { return cmd_; }

        zx_status_t Invoke(RealtekStream* stream,
                           const CodecResponse& resp) __TA_REQUIRES(stream->obj_lock()) {
            ZX_DEBUG_ASSERT((stream != nullptr) && (cmd_.thunk != nullptr));
            return ((*stream).*(cmd_.thunk))(cmd_, resp);
        }

    private:
        // Hide our constructor and make the allocator our friend so that people
        // do not accidentally allocate a pending command using std::new
        friend PendingCommandAllocator;
        PendingCommand(const Command& cmd) : cmd_(cmd) { }
        Command cmd_;
    };

    // TODO(johngro) : Elminiate this complexity if/when we get to the point
    // that audio streams have a 1:1 relationship with their clients (instead of
    // 1:many)
    struct NotifyTarget : fbl::DoublyLinkedListable<fbl::unique_ptr<NotifyTarget>> {
        explicit NotifyTarget(fbl::RefPtr<dispatcher::Channel>&& ch) : channel(ch) { }
        fbl::RefPtr<dispatcher::Channel> channel;
    };
    using NotifyTargetList = fbl::DoublyLinkedList<fbl::unique_ptr<NotifyTarget>>;

    // Bits used to track setup state machine progress.
    static constexpr uint32_t PIN_COMPLEX_SETUP_COMPLETE = (1u << 0);
    static constexpr uint32_t CONVERTER_SETUP_COMPLETE   = (1u << 1);
    static constexpr uint32_t PLUG_STATE_SETUP_COMPLETE  = (1u << 2);
    static constexpr uint32_t DMA_ASSIGNMENT_COMPLETE    = (1u << 3);
    static constexpr uint32_t STREAM_PUBLISHED           = (1u << 31);
    static constexpr uint32_t ALL_SETUP_COMPLETE         = PIN_COMPLEX_SETUP_COMPLETE
                                                         | CONVERTER_SETUP_COMPLETE
                                                         | PLUG_STATE_SETUP_COMPLETE
                                                         | DMA_ASSIGNMENT_COMPLETE;

    static uint8_t ComputeGainSteps(const CommonCaps& caps, float target_gai);

    zx_status_t RunCmdLocked(const Command& cmd)
        __TA_REQUIRES(obj_lock());

    zx_status_t RunCmdListLocked(const Command* list, size_t count, bool force_all = false)
        __TA_REQUIRES(obj_lock());

    zx_status_t DisableConverterLocked(bool force_all = false) __TA_REQUIRES(obj_lock());
    zx_status_t UpdateConverterGainLocked(float target_gain) __TA_REQUIRES(obj_lock());
    float       ComputeCurrentGainLocked() __TA_REQUIRES(obj_lock());
    zx_status_t SendGainUpdatesLocked() __TA_REQUIRES(obj_lock());
    void        AddPDNotificationTgtLocked(dispatcher::Channel* channel) __TA_REQUIRES(obj_lock());
    void        RemovePDNotificationTgtLocked(const dispatcher::Channel& channel)
        __TA_REQUIRES(obj_lock());

    // Setup state machine methods.
    zx_status_t UpdateSetupProgressLocked(uint32_t stage) __TA_REQUIRES(obj_lock());
    zx_status_t FinalizeSetupLocked() __TA_REQUIRES(obj_lock());
    void DumpStreamPublishedLocked() __TA_REQUIRES(obj_lock());
    void DumpAmpCaps(const CommonCaps& caps, const char* tag);
    DECLARE_THUNK(ProcessPinWidgetCaps);
    DECLARE_THUNK(ProcessPinAmpCaps);
    DECLARE_THUNK(ProcessPinCfgDefaults);
    DECLARE_THUNK(ProcessPinCaps);
    DECLARE_THUNK(ProcessPinState);
    DECLARE_THUNK(ProcessConverterWidgetCaps);
    DECLARE_THUNK(ProcessConverterAmpCaps);
    DECLARE_THUNK(ProcessConverterSampleSizeRate);
    DECLARE_THUNK(ProcessConverterSampleFormats);

    bool can_mute() const __TA_REQUIRES(obj_lock()) {
        return (conv_.has_amp && conv_.amp_caps.can_mute()) ||
               (pc_.has_amp && pc_.amp_caps.can_mute());
    }

    const StreamProperties props_;
    fbl::DoublyLinkedList<fbl::unique_ptr<PendingCommand>> pending_cmds_ __TA_GUARDED(obj_lock());

    // Setup state machine progress.
    uint32_t setup_progress_ __TA_GUARDED(obj_lock()) = 0;
    bool     format_set_     __TA_GUARDED(obj_lock()) = false;

    // Current gain and plug detect settings.
    uint8_t   cur_conv_gain_steps_ __TA_GUARDED(obj_lock()) = 0;
    uint8_t   cur_pc_gain_steps_   __TA_GUARDED(obj_lock()) = 0;
    bool      cur_mute_            __TA_GUARDED(obj_lock()) = false;
    bool      plug_state_          __TA_GUARDED(obj_lock()) = true;
    zx_time_t last_plug_time_      __TA_GUARDED(obj_lock()) = 0;
    NotifyTargetList plug_notify_targets_ __TA_GUARDED(obj_lock());

    // Converter and pin complex capabilities.
    ConverterCaps  conv_ __TA_GUARDED(obj_lock());
    PinComplexCaps pc_   __TA_GUARDED(obj_lock());
};

#undef DECLARE_THUNK

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

// TODO(johngro) : Right now, there is no really good way to hide a static slab
// allocator from the rest of the world.  It is not really much of a concern
// here, but it seems odd to have a private inner class which can be
// instantiated by things outside of the class.
//
// We should probably either fix static slab allocators so that they can be made
// private inner classes as well, or just go ahead and make these slab allocated
// bookkeeping classes non-inner-classes.
FWD_DECL_STATIC_SLAB_ALLOCATOR(audio::intel_hda::codecs::RealtekStream::PCAT);

