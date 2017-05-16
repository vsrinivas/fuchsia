// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "drivers/audio/intel-hda/utils/codec-caps.h"
#include "drivers/audio/intel-hda/utils/codec-commands.h"
#include "drivers/audio/intel-hda/utils/codec-state.h"

#include "debug-logging.h"
#include "realtek-stream.h"

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(audio::intel_hda::codecs::RealtekStream::PCAT, 16);

namespace audio {
namespace intel_hda {
namespace codecs {

static constexpr float DEFAULT_INITIAL_GAIN = -30.0;

mx_status_t RealtekStream::DisableConverterLocked(bool force_all) {
    const Command DISABLE_CONVERTER_VERBS[] = {
        { props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()) },
        { props_.pc_nid,   SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input()) },
        { props_.conv_nid, SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0) },
        { props_.conv_nid, SET_POWER_STATE(HDA_PS_D3HOT) },
        { props_.pc_nid,   SET_POWER_STATE(HDA_PS_D3HOT) },
    };

    return RunCmdListLocked(DISABLE_CONVERTER_VERBS, countof(DISABLE_CONVERTER_VERBS), force_all);
}

mx_status_t RealtekStream::UpdateConverterGainLocked(float target_gain) {
    if (!conv_.has_amp)
        return ERR_NOT_SUPPORTED;

    if ((target_gain < conv_.min_gain) || (target_gain > conv_.max_gain))
        return ERR_INVALID_ARGS;

    MX_DEBUG_ASSERT(conv_.gain_step > 0);

    uint32_t tmp = ((target_gain - conv_.min_gain) + (conv_.gain_step / 2)) / conv_.gain_step;
    MX_DEBUG_ASSERT(tmp <= conv_.amp_caps.num_steps());

    cur_gain_steps_ = static_cast<uint8_t>(tmp);
    return NO_ERROR;
}

float RealtekStream::ComputeCurrentGainLocked() {
    return conv_.has_amp
        ? conv_.min_gain + (cur_gain_steps_ * conv_.gain_step)
        : 0.0f;
}

mx_status_t RealtekStream::SendGainUpdatesLocked() {
    mx_status_t res;

    if (conv_.has_amp) {
        bool mute = conv_.amp_caps.can_mute() ? cur_mute_ : false;
        res = RunCmdLocked({ props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(mute,
                                                                      cur_gain_steps_,
                                                                      is_input(), !is_input()) });
        if (res != NO_ERROR)
            return res;
    }

    if (pc_.has_amp) {
        bool mute = pc_.amp_caps.can_mute() ? cur_mute_ : false;
        res = RunCmdLocked({ props_.pc_nid, SET_AMPLIFIER_GAIN_MUTE(mute,
                                                                    pc_.amp_caps.offset(),
                                                                    is_input(), !is_input()) });
        if (res != NO_ERROR)
            return res;
    }

    return NO_ERROR;
}

// TODO(johngro) : re: the plug_notify_targets_ list.  In theory, we could put
// this in a tree indexed by the channel's owner context, or by the pointer
// itself.  This would make add/remove operations simpler, and faster in the
// case that we had lots of clients.  In reality, however, we are likely to
// limit the interface moving forward so that we have only one client at a time
// (in which case this becomes much simpler).  Moving forward, we need to come
// back and either simplify or optimize (as the situation warrents) once we know
// how we are proceeding.
void RealtekStream::AddPDNotificationTgtLocked(DispatcherChannel* channel) {
    bool duplicate = false;
    for (auto& tgt : plug_notify_targets_) {
        duplicate = (tgt.channel.get() == channel);
        if (duplicate)
            break;
    }

    if (!duplicate) {
        mxtl::RefPtr<DispatcherChannel> c(channel);
        mxtl::unique_ptr<NotifyTarget> tgt(new NotifyTarget(mxtl::move(c)));
        plug_notify_targets_.push_back(mxtl::move(tgt));
    }
}

void RealtekStream::RemovePDNotificationTgtLocked(const DispatcherChannel& channel) {
    for (auto& tgt : plug_notify_targets_) {
        if (tgt.channel.get() == &channel) {
            plug_notify_targets_.erase(tgt);
            break;
        }
    }
}

mx_status_t RealtekStream::RunCmdLocked(const Command& cmd) {
    mxtl::unique_ptr<PendingCommand> pending_cmd;
    bool want_response = (cmd.thunk != nullptr);

    if (want_response) {
        pending_cmd = PendingCommandAllocator::New(cmd);
        if (pending_cmd == nullptr)
            return ERR_NO_MEMORY;
    }

    mx_status_t res = SendCodecCommandLocked(cmd.nid,
                                             cmd.verb,
                                             want_response ? Ack::YES : Ack::NO);
    VERBOSE_LOG("SEND: nid %2hu verb 0x%05x%s\n", cmd.nid, cmd.verb.val, want_response ? "*" : "");

    if ((res == NO_ERROR) && want_response)
        pending_cmds_.push_back(mxtl::move(pending_cmd));

    return res;
}

mx_status_t RealtekStream::RunCmdListLocked(const Command* list, size_t count, bool force_all) {
    MX_DEBUG_ASSERT(list);

    mx_status_t total_res = NO_ERROR;
    for (size_t i = 0; i < count; ++i) {
        mx_status_t res = RunCmdLocked(list[i]);

        if (res != NO_ERROR) {
            if (!force_all)
                return res;

            if (total_res == NO_ERROR)
                total_res = res;
        }
    }

    return total_res;
}

void RealtekStream::OnDeactivateLocked() {
    plug_notify_targets_.clear();
    DisableConverterLocked(true);
}

void RealtekStream::OnChannelDeactivateLocked(const DispatcherChannel& channel) {
    RemovePDNotificationTgtLocked(channel);
}

mx_status_t RealtekStream::OnDMAAssignedLocked() {
    return UpdateSetupProgressLocked(DMA_ASSIGNMENT_COMPLETE);
}

mx_status_t RealtekStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
    if (pending_cmds_.is_empty()) {
        LOG("Received solicited response (0x%08x), but no commands are pending!\n", resp.data);
        return ERR_BAD_STATE;
    }

    auto pending_cmd = pending_cmds_.pop_front();
    VERBOSE_LOG("RECV: nid %2hu verb 0x%05x --> 0x%08x\n",
                pending_cmd->cmd().nid,
                pending_cmd->cmd().verb.val,
                resp.data);
    return pending_cmd->Invoke(this, resp);
}

mx_status_t RealtekStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
    // TODO(johngro) : Which bit should we be using as the pin sense bit?  The
    // Intel HDA spec only specifies what digital display pins are required to
    // use; generally speaking unsolicited response payloads are supposed to be
    // vendor specific.
    //
    // The only Realtek datasheets I have seen do not define which bit they will
    // use.  Experimentally, it seems like Realtek codecs use bit 3 for the pin
    // sense bit, so this is what we use for now.
    bool plugged = resp.data & (1u << 3);

    if (plug_state_ != plugged) {
        // Update our internal state.
        plug_state_ = plugged;
        last_plug_time_ = mx_time_get(MX_CLOCK_MONOTONIC);

        // Inform anyone who has registered for notification.
        MX_DEBUG_ASSERT(pc_.async_plug_det);
        if (!plug_notify_targets_.is_empty()) {
            audio2_proto::PlugDetectNotify notif;

            notif.hdr.cmd = AUDIO2_STREAM_PLUG_DETECT_NOTIFY;
            notif.hdr.transaction_id = AUDIO2_INVALID_TRANSACTION_ID;
            notif.flags = static_cast<audio2_pd_notify_flags_t>(
                    (plug_state_ ? AUDIO2_PDNF_PLUGGED : 0) | AUDIO2_PDNF_CAN_NOTIFY);
            notif.plug_state_time = last_plug_time_;

            for (auto iter = plug_notify_targets_.begin(); iter != plug_notify_targets_.end(); ) {
                mx_status_t res;

                MX_DEBUG_ASSERT(iter->channel != nullptr);
                res = iter->channel->Write(&notif, sizeof(notif));
                if (res != NO_ERROR) {
                    // If we have failed to send the notification over our
                    // client channel, something has gone fairly wrong.  Remove
                    // the client from the notification list.
                    plug_notify_targets_.erase(iter++);
                } else {
                    ++iter;
                }
            }
        }
    }

    return NO_ERROR;
}

mx_status_t RealtekStream::BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt) {
    // Check the format arguments.
    if (!fmt.channels || (fmt.channels > conv_.widget_caps.ch_count()))
        return ERR_NOT_SUPPORTED;

    if (!conv_.sample_caps.SupportsRate(fmt.frames_per_second) ||
        !conv_.sample_caps.SupportsFormat(fmt.sample_format))
        return ERR_NOT_SUPPORTED;

    // Looks good, make sure that the converter is muted and not processing any stream tags.
    format_set_ = false;
    return DisableConverterLocked();
}

mx_status_t RealtekStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
    mx_status_t res;
    const Command ENABLE_CONVERTER_VERBS[] = {
        { props_.conv_nid, SET_CONVERTER_FORMAT(encoded_fmt) },
        { props_.conv_nid, SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0) },
        { props_.pc_nid,   SET_POWER_STATE(HDA_PS_D0) },
        { props_.conv_nid, SET_POWER_STATE(HDA_PS_D0) },
        { props_.pc_nid,   SET_ANALOG_PIN_WIDGET_CTRL(!is_input(), is_input(),
                                                       pc_.pin_caps.can_drive_headphones() ) },
    };

    res = RunCmdListLocked(ENABLE_CONVERTER_VERBS, countof(ENABLE_CONVERTER_VERBS));
    if (res != NO_ERROR)
        return res;

    res = SendGainUpdatesLocked();
    if (res != NO_ERROR)
        return res;

    format_set_ = true;
    return NO_ERROR;
}

void RealtekStream::OnGetGainLocked(audio2_proto::GetGainResp* out_resp) {
    MX_DEBUG_ASSERT(out_resp);

    if (conv_.has_amp) {
        out_resp->cur_gain  = ComputeCurrentGainLocked();
        out_resp->min_gain  = conv_.min_gain;
        out_resp->max_gain  = conv_.max_gain;
        out_resp->gain_step = conv_.gain_step;
    } else {
        out_resp->cur_gain  = 0.0;
        out_resp->min_gain  = 0.0;
        out_resp->max_gain  = 0.0;
        out_resp->gain_step = 0.0;
    }

    out_resp->cur_mute = cur_mute_;
    out_resp->can_mute = can_mute();
}

void RealtekStream::OnSetGainLocked(const audio2_proto::SetGainReq& req,
                                    audio2_proto::SetGainResp* out_resp) {
    mx_status_t res  = NO_ERROR;
    bool mute_target = cur_mute_;
    bool set_mute    = req.flags & AUDIO2_SGF_MUTE_VALID;
    bool set_gain    = req.flags & AUDIO2_SGF_GAIN_VALID;

    if (set_mute || set_gain) {
        if (set_mute) {
            if (!can_mute()) {
                res = ERR_INVALID_ARGS;
            } else {
                mute_target = req.flags & AUDIO2_SGF_MUTE;
            }
        }

        if ((res == NO_ERROR) && set_gain)
            res = UpdateConverterGainLocked(req.gain);
    }

    if (res == NO_ERROR) {
        cur_mute_ = mute_target;

        // Don't bother sending any update to the converter if the format is not currently set.
        if (format_set_)
            res = SendGainUpdatesLocked();
    }

    if (out_resp != nullptr) {
        out_resp->result    = res;
        out_resp->cur_mute  = cur_mute_;
        out_resp->cur_gain  = ComputeCurrentGainLocked();
    }
}

void RealtekStream::OnPlugDetectLocked(DispatcherChannel* response_channel,
                                       const audio2_proto::PlugDetectReq& req,
                                       audio2_proto::PlugDetectResp* out_resp) {
    MX_DEBUG_ASSERT(response_channel != nullptr);

    // If our pin cannot perform presence detection, just fall back on the base class impl.
    if (!pc_.pin_caps.can_pres_detect()) {
        IntelHDAStreamBase::OnPlugDetectLocked(response_channel, req, out_resp);
        return;
    }

    if (pc_.async_plug_det) {
        // If we are capible of asynch plug detection, add or remove this client
        // to/from the notify list before reporting the current state.  Apps
        // should not be setting both flags, but if they do, disable wins.
        if (req.flags & AUDIO2_PDF_DISABLE_NOTIFICATIONS) {
            RemovePDNotificationTgtLocked(*response_channel);
        } else if (req.flags & AUDIO2_PDF_ENABLE_NOTIFICATIONS) {
            AddPDNotificationTgtLocked(response_channel);
        }

        // Report the current plug detection state if the client expects a response.
        if (out_resp) {
            out_resp->flags  = static_cast<audio2_pd_notify_flags_t>(
                               (plug_state_ ? AUDIO2_PDNF_PLUGGED : 0) |
                               (pc_.async_plug_det ? AUDIO2_PDNF_CAN_NOTIFY : 0));
            out_resp->plug_state_time = last_plug_time_;
        }
    } else {
        // TODO(johngro): In order to do proper polling support, we need to add
        // the concept of a pending client request to the system.  IOW - we need
        // to create and run a state machine where we hold a reference to the
        // client's response channel, and eventually respond to the client using
        // the same transaction ID they requested state with.
        //
        // For now, if our hardware does not support async plug detect, we
        // simply fall back on the default implementation which reports that we
        // are hardwired and always plugged in.
        IntelHDAStreamBase::OnPlugDetectLocked(response_channel, req, out_resp);
        return;
    }
}

mx_status_t RealtekStream::UpdateSetupProgressLocked(uint32_t stage) {
    MX_DEBUG_ASSERT(!(setup_progress_ & STREAM_PUBLISHED));
    MX_DEBUG_ASSERT(!(setup_progress_ & stage));

    setup_progress_ |= stage;

    if (setup_progress_ == ALL_SETUP_COMPLETE) {
        setup_progress_ |= STREAM_PUBLISHED;
        DumpStreamPublishedLocked();
        return PublishDeviceLocked();
    }

    return NO_ERROR;
}

void RealtekStream::DumpStreamPublishedLocked() {
    if (!DEBUG_LOGGING)
        return;

    static const struct {
        uint32_t flag;
        uint32_t rate;
    } RATE_LUT[] = {
        { IHDA_PCM_RATE_384000, 384000 },
        { IHDA_PCM_RATE_192000, 192000 },
        { IHDA_PCM_RATE_176400, 176400 },
        { IHDA_PCM_RATE_96000,  96000 },
        { IHDA_PCM_RATE_88200,  88200 },
        { IHDA_PCM_RATE_48000,  48000 },
        { IHDA_PCM_RATE_44100,  44100 },
        { IHDA_PCM_RATE_32000,  32000 },
        { IHDA_PCM_RATE_22050,  22050 },
        { IHDA_PCM_RATE_16000,  16000 },
        { IHDA_PCM_RATE_11025,  11025 },
        { IHDA_PCM_RATE_8000,   8000 },
    };

    static const struct {
        uint32_t flag;
        uint32_t bits;
    } BITS_LUT[] = {
        { IHDA_PCM_SIZE_32BITS, 32 },
        { IHDA_PCM_SIZE_24BITS, 24 },
        { IHDA_PCM_SIZE_20BITS, 20 },
        { IHDA_PCM_SIZE_16BITS, 16 },
        { IHDA_PCM_SIZE_8BITS,  8 },
    };

    LOG("Setup complete, publishing stream\n");
    LOG("Max channels : %u\n", conv_.widget_caps.ch_count());

    LOG("Sample rates :");
    for (size_t i = 0; i < countof(RATE_LUT); ++i) {
        const auto& entry = RATE_LUT[i];
        if (conv_.sample_caps.pcm_size_rate_ & entry.flag)
            printf(" %u", entry.rate);
    }
    printf("\n");

    LOG("Sample bits  :");
    for (size_t i = 0; i < countof(BITS_LUT); ++i) {
        const auto& entry = BITS_LUT[i];
        if (conv_.sample_caps.pcm_size_rate_ & entry.flag)
            printf(" %u", entry.bits);
    }
    printf("\n");

    if (conv_.has_amp) {
        LOG("Gain control : [%.2f, %.2f] dB in %.2f dB steps (%s mute).\n",
            conv_.min_gain,
            conv_.max_gain,
            conv_.gain_step,
            can_mute() ? "can" : "cannot");
    } else {
        LOG("Gain control : 0dB fixed (%s mute)\n", can_mute() ? "can" : "cannot");
    }

    if (pc_.pin_caps.can_pres_detect()) {
        LOG("Plug Detect  : %s (current state %s)\n",
            pc_.async_plug_det ? "Asynchronous" : "Poll-only",
            plug_state_ ? "Plugged" : "Unplugged");
    } else {
        LOG("Plug Detect  : No\n");
    }

}

#define THUNK(_method) (&RealtekStream::_method)

mx_status_t RealtekStream::OnActivateLocked() {
    // Start by attempting to put our pin complex and converter into a disabled
    // state.
    mx_status_t res = DisableConverterLocked();
    if (res != NO_ERROR)
        return res;

    // Start the setup process by fetching the widget caps for our converter and
    // pin complex.  This will let us know where various parameters (sample
    // size/rate, stream format, amplifier caps, etc...) come from.  Also, go
    // ahead and fetch the pin caps so we have an idea of our presence detection
    // capabilities.
    const Command SETUP[] = {
        { props_.pc_nid,   GET_PARAM(CodecParam::AW_CAPS),  THUNK(ProcessPinWidgetCaps) },
        { props_.conv_nid, GET_PARAM(CodecParam::AW_CAPS),  THUNK(ProcessConverterWidgetCaps) },
        { props_.pc_nid,   GET_PARAM(CodecParam::PIN_CAPS), THUNK(ProcessPinCaps) },
    };

    return RunCmdListLocked(SETUP, countof(SETUP));
}

mx_status_t RealtekStream::ProcessPinWidgetCaps(const Command& cmd, const CodecResponse& resp) {
    // Stash the pin's audio-widget caps.  We will need it while processing the
    // pin caps to determine if we need to register for async plug detection
    // notifications before querying the initial pin state.
    pc_.widget_caps.raw_data_ = resp.data;

    // Does this pin complex have an amplifier?  If so, we need to query what
    // it's caps, so we know what it's mute capabilities and unity gain are.  If
    // not, we are done.
    pc_.has_amp = is_input()
                ? pc_.widget_caps.input_amp_present()
                : pc_.widget_caps.output_amp_present();

    if (!pc_.has_amp)
        return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);

    return RunCmdLocked({ pc_.widget_caps.amp_param_override() ? props_.pc_nid : props_.afg_nid,
                         GET_PARAM(AMP_CAPS(is_input())),
                         THUNK(ProcessPinAmpCaps) });
}

mx_status_t RealtekStream::ProcessPinAmpCaps(const Command& cmd, const CodecResponse& resp) {
    pc_.amp_caps.raw_data_ = resp.data;
    return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);
}

mx_status_t RealtekStream::ProcessPinCaps(const Command& cmd, const CodecResponse& resp) {
    pc_.pin_caps.raw_data_ = resp.data;

    // Sanity check out input/output configuration.
    if ((is_input() ? pc_.pin_caps.can_input() : pc_.pin_caps.can_output()) == false) {
        const char* tag = is_input() ? "input" : "output";
        LOG("ERROR: Stream configured for %s, but pin complex cannot %s\n", tag, tag);
        return ERR_BAD_STATE;
    }

    // Can this stream determine if it is connected or not?  If not, then we
    // just assume that we are always plugged in.
    if (!pc_.pin_caps.can_pres_detect() || pc_.pin_caps.trig_required()) {
        if (pc_.pin_caps.trig_required()) {
            LOG("WARNING : Triggered impedence sense plug detect not supported.  "
                "Stream will always appear to be plugged in.\n");
        }
        return UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
    }

    // Looks like we support presence detection.  Enable unsolicited
    // notifications of pin state if supported, then query the initial pin
    // state.
    pc_.async_plug_det = pc_.widget_caps.can_send_unsol();
    if (pc_.async_plug_det) {
        mx_status_t res = AllocateUnsolTagLocked(&pc_.unsol_tag);
        if (res == NO_ERROR) {
            mx_status_t res = RunCmdLocked({ props_.pc_nid,
                                             SET_UNSOLICITED_RESP_CTRL(true, pc_.unsol_tag) });
            if (res != NO_ERROR)
                return res;
        } else {
            LOG("WARNING : Failed to allocate unsolicited response tag from "
                "codec pool (res %d).  Asynchronous plug detection will be "
                "disabled.\n", res);
            pc_.async_plug_det = false;
        }
    }

    // Now that notifications have been enabled (or not), query the initial pin state.
    return RunCmdLocked({ props_.pc_nid, GET_PIN_SENSE, THUNK(ProcessPinState) });
}

mx_status_t RealtekStream::ProcessPinState(const Command& cmd, const CodecResponse& resp) {
    plug_state_ = PinSenseState(resp.data).presence_detect();
    last_plug_time_ = mx_time_get(MX_CLOCK_MONOTONIC);
    return UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
}

mx_status_t RealtekStream::ProcessConverterWidgetCaps(const Command& cmd,
                                                      const CodecResponse& resp) {
    mx_status_t res;

    conv_.widget_caps.raw_data_ = resp.data;
    conv_.has_amp = is_input() ? conv_.widget_caps.input_amp_present()
                               : conv_.widget_caps.output_amp_present();

    // Fetch the amp caps (if any) either from the converter or the defaults
    // from the function group if the converter has not overridden them.
    if (conv_.has_amp) {
        uint16_t nid = conv_.widget_caps.amp_param_override() ? props_.conv_nid : props_.afg_nid;
        res = RunCmdLocked({ nid,
                             GET_PARAM(AMP_CAPS(is_input())),
                             THUNK(ProcessConverterAmpCaps) });
        if (res != NO_ERROR)
            return res;
    }

    // Fetch the supported sample rates, bit depth, and formats.
    uint16_t nid = conv_.widget_caps.format_override() ? props_.conv_nid : props_.afg_nid;
    const Command FETCH_FORMATS[] = {
     { nid, GET_PARAM(CodecParam::SUPPORTED_PCM_SIZE_RATE), THUNK(ProcessConverterSampleSizeRate) },
     { nid, GET_PARAM(CodecParam::SUPPORTED_STREAM_FORMATS), THUNK(ProcessConverterSampleFormats) },
    };

    res = RunCmdListLocked(FETCH_FORMATS, countof(FETCH_FORMATS));
    if (res != NO_ERROR)
        return res;

    return NO_ERROR;
}

mx_status_t RealtekStream::ProcessConverterAmpCaps(const Command& cmd, const CodecResponse& resp) {
    conv_.amp_caps.raw_data_ = resp.data;

    conv_.gain_step = conv_.amp_caps.step_size_db();
    conv_.min_gain  = conv_.amp_caps.min_gain_db();
    conv_.max_gain  = conv_.amp_caps.max_gain_db();

    return UpdateConverterGainLocked(mxtl::max(DEFAULT_INITIAL_GAIN, conv_.min_gain));
}

mx_status_t RealtekStream::ProcessConverterSampleSizeRate(const Command& cmd,
                                                          const CodecResponse& resp) {
    conv_.sample_caps.pcm_size_rate_ = resp.data;
    return NO_ERROR;
}

mx_status_t RealtekStream::ProcessConverterSampleFormats(const Command& cmd,
                                                         const CodecResponse& resp) {
    conv_.sample_caps.pcm_formats_ = resp.data;
    return UpdateSetupProgressLocked(CONVERTER_SETUP_COMPLETE);
}
#undef THUNK

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
