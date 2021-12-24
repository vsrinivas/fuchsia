// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hdmi-stream.h"

#include <lib/ddk/debug.h>
#include <lib/edid/edid.h>
#include <lib/zx/clock.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/vector.h>
#include <intel-hda/utils/codec-caps.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/codec-state.h>
#include <intel-hda/utils/utils.h>
#include <src/lib/eld/eld.h>

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(audio::intel_hda::codecs::HdmiStream::PCAT, 16);
#define THUNK(_method) (&HdmiStream::_method)

namespace audio::intel_hda::codecs {

HdmiStream::HdmiStream(const StreamProperties& props)
    : IntelHDAStreamBase(props.stream_id, false), props_(props) {}

zx_status_t HdmiStream::DisableConverterLocked(bool force_all) {
  const Command DISABLE_CONVERTER_VERBS[] = {
      {props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, false, true)},
      {props_.pc_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, false, true)},
      {props_.conv_nid, SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0)},
      {props_.conv_nid, SET_POWER_STATE(HDA_PS_D3HOT)},
      {props_.pc_nid, SET_POWER_STATE(HDA_PS_D3HOT)},
  };

  return RunCmdListLocked(DISABLE_CONVERTER_VERBS, std::size(DISABLE_CONVERTER_VERBS), force_all);
}

zx_status_t HdmiStream::UpdateConverterGainLocked(float target_gain) {
  if (!conv_.has_amp) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if ((target_gain < conv_.min_gain) || (target_gain > conv_.max_gain)) {
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(conv_.gain_step > 0);

  float tmp = ((target_gain - conv_.min_gain) + (conv_.gain_step / 2)) / conv_.gain_step;
  ZX_DEBUG_ASSERT(static_cast<uint32_t>(tmp) <= conv_.amp_caps.num_steps());

  cur_conv_gain_steps_ = ComputeGainSteps(conv_, target_gain);
  return ZX_OK;
}

float HdmiStream::ComputeCurrentGainLocked() {
  return conv_.has_amp ? conv_.min_gain + (cur_conv_gain_steps_ * conv_.gain_step) : 0.0f;
}

zx_status_t HdmiStream::SendGainUpdatesLocked() {
  zx_status_t res;

  if (conv_.has_amp) {
    bool mute = conv_.amp_caps.can_mute() ? cur_mute_ : false;
    res = RunCmdLocked(
        {props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(mute, cur_conv_gain_steps_, false, true)});
    if (res != ZX_OK)
      return res;
  }

  if (pc_.has_amp) {
    bool mute = pc_.amp_caps.can_mute() ? cur_mute_ : false;
    res = RunCmdLocked(
        {props_.pc_nid, SET_AMPLIFIER_GAIN_MUTE(mute, cur_pc_gain_steps_, false, true)});
    if (res != ZX_OK) {
      return res;
    }
  }

  return ZX_OK;
}

// static
uint8_t HdmiStream::ComputeGainSteps(const CommonCaps& caps, float target_gain) {
  if (!caps.has_amp || !caps.amp_caps.num_steps()) {
    return 0;
  }

  if (target_gain < caps.min_gain) {
    return 0;
  }

  if (target_gain > caps.max_gain) {
    return static_cast<uint8_t>(caps.amp_caps.num_steps() - 1);
  }

  ZX_DEBUG_ASSERT(caps.gain_step > 0);
  float tmp = ((target_gain - caps.min_gain) + (caps.gain_step / 2)) / caps.gain_step;
  ZX_DEBUG_ASSERT(static_cast<uint32_t>(tmp) < caps.amp_caps.num_steps());

  return static_cast<uint8_t>(tmp);
}

zx_status_t HdmiStream::RunCmdLocked(const Command& cmd) {
  std::unique_ptr<PendingCommand> pending_cmd;
  bool want_response = (cmd.thunk != nullptr);

  if (want_response) {
    pending_cmd = PendingCommandAllocator::New(cmd);
    if (pending_cmd == nullptr)
      return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = SendCodecCommandLocked(cmd.nid, cmd.verb, want_response ? Ack::YES : Ack::NO);
  zxlogf(DEBUG, "SEND: nid %2hu verb 0x%05x%s", cmd.nid, cmd.verb.val, want_response ? "*" : "");

  if ((res == ZX_OK) && want_response) {
    pending_cmds_.push_back(std::move(pending_cmd));
  }

  return res;
}

zx_status_t HdmiStream::RunCmdListLocked(const Command* list, size_t count, bool force_all) {
  ZX_DEBUG_ASSERT(list);

  zx_status_t total_res = ZX_OK;
  for (size_t i = 0; i < count; ++i) {
    zx_status_t res = RunCmdLocked(list[i]);

    if (res != ZX_OK) {
      if (!force_all) {
        return res;
      }

      if (total_res == ZX_OK) {
        total_res = res;
      }
    }
  }

  return total_res;
}

void HdmiStream::OnDeactivateLocked() { DisableConverterLocked(true); }

void HdmiStream::OnChannelDeactivateLocked(const StreamChannel& channel) {}

zx_status_t HdmiStream::OnDMAAssignedLocked() {
  return UpdateSetupProgressLocked(DMA_ASSIGNMENT_COMPLETE);
}

zx_status_t HdmiStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
  if (pending_cmds_.is_empty()) {
    zxlogf(ERROR, "Received solicited response (0x%08x), but no commands are pending!", resp.data);
    return ZX_ERR_BAD_STATE;
  }

  auto pending_cmd = pending_cmds_.pop_front();
  zxlogf(DEBUG, "RECV: nid %2hu verb 0x%05x --> 0x%08x", pending_cmd->cmd().nid,
         pending_cmd->cmd().verb.val, resp.data);
  return pending_cmd->Invoke(this, resp);
}

zx_status_t HdmiStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
  bool plugged = resp.data & (1u << 0);  // Presence Detect, section 7.3.3.14.1.
  if (plug_state_ != plugged) {
    // Update our internal state.
    plug_state_ = plugged;
    last_plug_time_ = zx::clock::get_monotonic().get();

    // Inform anyone who has registered for notification.
    ZX_DEBUG_ASSERT(pc_.async_plug_det);
    NotifyPlugStateLocked(plug_state_, last_plug_time_);
  }

  bool eld_valid = resp.data & (1u << 1);  // ELD Valid, section 7.3.3.14.1.
  if (eld_valid) {
    // TODO(35986): Add support for updating existing ELDs and hence formats
    // when a different monitor is plugged, blocked on 66649.
    if (!eld_valid_) {
      eld_valid_ = true;
      // We start a new ELD retrieval by asking for the ELD buffer size by setting bit 3,
      // DIP-Size section 7.3.3.36.
      return RunCmdLocked(
          {props_.pc_nid, GET_DIP_SIZE_INFO(1 << 3), THUNK(ProcessDataIslandPacketSizeInfo)});
    }
  }
  return ZX_OK;
}

zx_status_t HdmiStream::BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt) {
  // Check the format arguments.
  if (!fmt.channels || (fmt.channels >= conv_.widget_caps.ch_count())) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!merged_sample_caps_.SupportsRate(fmt.frames_per_second) ||
      !merged_sample_caps_.SupportsFormat(fmt.sample_format)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Looks good, make sure that the converter is muted and not processing any stream tags.
  format_set_ = false;
  return DisableConverterLocked();
}

zx_status_t HdmiStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  zx_status_t res;
  const Command ENABLE_CONVERTER_VERBS[] = {
      {props_.conv_nid, SET_CONVERTER_FORMAT(encoded_fmt)},
      {props_.conv_nid, SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0)},
      {props_.pc_nid, SET_POWER_STATE(HDA_PS_D0)},
      {props_.conv_nid, SET_POWER_STATE(HDA_PS_D0)},
      {props_.pc_nid, SET_DIGITAL_PIN_WIDGET_CTRL(true, false)},
  };

  res = RunCmdListLocked(ENABLE_CONVERTER_VERBS, std::size(ENABLE_CONVERTER_VERBS));
  if (res != ZX_OK) {
    return res;
  }

  res = SendGainUpdatesLocked();
  if (res != ZX_OK) {
    return res;
  }

  format_set_ = true;
  return ZX_OK;
}

void HdmiStream::OnGetGainLocked(audio_proto::GainState* out_resp) {
  ZX_DEBUG_ASSERT(out_resp);

  if (conv_.has_amp) {
    out_resp->cur_gain = ComputeCurrentGainLocked();
    out_resp->min_gain = conv_.min_gain;
    out_resp->max_gain = conv_.max_gain;
    out_resp->gain_step = conv_.gain_step;
  } else {
    out_resp->cur_gain = 0.0;
    out_resp->min_gain = 0.0;
    out_resp->max_gain = 0.0;
    out_resp->gain_step = 0.0;
  }

  out_resp->cur_mute = cur_mute_;
  out_resp->can_mute = can_mute();
}

void HdmiStream::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                 audio_proto::SetGainResp* out_resp) {
  zx_status_t res = ZX_OK;
  bool mute_target = cur_mute_;
  bool set_mute = req.flags & AUDIO_SGF_MUTE_VALID;
  bool set_gain = req.flags & AUDIO_SGF_GAIN_VALID;

  if (set_mute || set_gain) {
    if (set_mute) {
      if (!can_mute()) {
        res = ZX_ERR_INVALID_ARGS;
      } else {
        mute_target = req.flags & AUDIO_SGF_MUTE;
      }
    }

    if ((res == ZX_OK) && set_gain) {
      res = UpdateConverterGainLocked(req.gain);
    }
  }

  if (res == ZX_OK) {
    cur_mute_ = mute_target;

    // Don't bother sending any update to the converter if the format is not currently set.
    if (format_set_) {
      res = SendGainUpdatesLocked();
    }
  }

  if (out_resp != nullptr) {
    out_resp->result = res;
    out_resp->cur_mute = cur_mute_;
    out_resp->cur_gain = ComputeCurrentGainLocked();
  }
}

void HdmiStream::OnPlugDetectLocked(StreamChannel* response_channel,
                                    audio_proto::PlugDetectResp* out_resp) {
  ZX_DEBUG_ASSERT(response_channel != nullptr);

  // If our pin cannot perform presence detection, just fall back on the base class impl.
  if (!pc_.pin_caps.can_pres_detect()) {
    IntelHDAStreamBase::OnPlugDetectLocked(response_channel, out_resp);
    return;
  }

  if (pc_.async_plug_det) {
    // Report the current plug detection state if the client expects a response.
    if (out_resp) {
      out_resp->flags = static_cast<audio_pd_notify_flags_t>(
          (plug_state_ ? static_cast<uint32_t>(AUDIO_PDNF_PLUGGED) : 0) |
          (pc_.async_plug_det ? static_cast<uint32_t>(AUDIO_PDNF_CAN_NOTIFY) : 0));
      out_resp->plug_state_time = last_plug_time_;
    }
  } else {
    // TODO(andresoportus): In order to do proper polling support, we need to add
    // the concept of a pending client request to the system.  IOW - we need
    // to create and run a state machine where we hold a reference to the
    // client's response channel, and eventually respond to the client using
    // the same transaction ID they requested state with.
    //
    // For now, if our hardware does not support async plug detect, we
    // simply fall back on the default implementation which reports that we
    // are hardwired and always plugged in.
    IntelHDAStreamBase::OnPlugDetectLocked(response_channel, out_resp);
  }
}

void HdmiStream::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                   audio_proto::GetStringResp* out_resp) {
  ZX_DEBUG_ASSERT(out_resp);
  const char* requested_string = nullptr;

  switch (req.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      requested_string = props_.mfr_name;
      break;

    case AUDIO_STREAM_STR_ID_PRODUCT:
      requested_string = props_.product_name;
      break;

    default:
      IntelHDAStreamBase::OnGetStringLocked(req, out_resp);
      return;
  }

  int res = snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s",
                     requested_string ? requested_string : "<unassigned>");
  ZX_DEBUG_ASSERT(res >= 0);
  out_resp->result = ZX_OK;
  out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
  out_resp->id = req.id;
}

zx_status_t HdmiStream::UpdateSetupProgressLocked(uint32_t stage) {
  ZX_DEBUG_ASSERT(!(setup_progress_ & STREAM_PUBLISHED));
  ZX_DEBUG_ASSERT(!(setup_progress_ & stage));

  setup_progress_ |= stage;

  if (setup_progress_ == ALL_SETUP_COMPLETE) {
    zx_status_t res = FinalizeSetupLocked();
    if (res != ZX_OK) {
      return res;
    }

    setup_progress_ |= STREAM_PUBLISHED;
// Uncomment to get a dump of the published formats right before publishing.
#if 0
    DumpStreamPublishedLocked();
#endif
    return PublishDeviceLocked();
  }

  return ZX_OK;
}

zx_status_t HdmiStream::FinalizeSetupLocked() {
  // Stash the number of gain steps to use in the pin converter.  This allows
  // us to hardcode gain targets for things like mic boost.  Eventually, we
  // need to expose a way to detect this capability and control it via APIs,
  // but for now we can get away with just setting it as part of the finalize
  // step for setup.
  cur_pc_gain_steps_ = ComputeGainSteps(pc_, props_.default_pc_gain);

  // Compute the list of formats we support.
  fbl::Vector<audio_proto::FormatRange> supported_formats;
  zx_status_t res =
      MakeFormatRangeList(merged_sample_caps_, conv_.widget_caps.ch_count(), &supported_formats);
  if (res != ZX_OK) {
    zxlogf(ERROR, "Failed to compute supported format ranges!  (res = %d)", res);
    return res;
  }

  // At this point, we should have at least one sample encoding that we
  // support.  If we don't, then this output stream is pretty worthless.
  if (!supported_formats.size()) {
    zxlogf(WARNING,
           "no sample encodings are supported by this audio stream!  "
           "(formats = 0x%08x, size/rates = 0x%08x)",
           merged_sample_caps_.pcm_formats_, merged_sample_caps_.pcm_size_rate_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  SetSupportedFormatsLocked(std::move(supported_formats));

  return ZX_OK;
}

void HdmiStream::DumpStreamPublishedLocked() {
  static const struct {
    uint32_t flag;
    uint32_t rate;
  } RATE_LUT[] = {
      {IHDA_PCM_RATE_384000, 384000}, {IHDA_PCM_RATE_192000, 192000},
      {IHDA_PCM_RATE_176400, 176400}, {IHDA_PCM_RATE_96000, 96000},
      {IHDA_PCM_RATE_88200, 88200},   {IHDA_PCM_RATE_48000, 48000},
      {IHDA_PCM_RATE_44100, 44100},   {IHDA_PCM_RATE_32000, 32000},
      {IHDA_PCM_RATE_22050, 22050},   {IHDA_PCM_RATE_16000, 16000},
      {IHDA_PCM_RATE_11025, 11025},   {IHDA_PCM_RATE_8000, 8000},
  };

  static const struct {
    uint32_t flag;
    uint32_t bits;
  } BITS_LUT[] = {
      {IHDA_PCM_SIZE_32BITS, 32}, {IHDA_PCM_SIZE_24BITS, 24}, {IHDA_PCM_SIZE_20BITS, 20},
      {IHDA_PCM_SIZE_16BITS, 16}, {IHDA_PCM_SIZE_8BITS, 8},
  };

  printf("Setup complete, publishing output stream\n");
  printf("Channels          : %u\n", conv_.widget_caps.ch_count());

  printf("Sample rates      :");
  for (size_t i = 0; i < std::size(RATE_LUT); ++i) {
    const auto& entry = RATE_LUT[i];
    if (merged_sample_caps_.pcm_size_rate_ & entry.flag)
      printf(" %u", entry.rate);
  }
  printf("\n");

  printf("Sample bits       :");
  for (size_t i = 0; i < std::size(BITS_LUT); ++i) {
    const auto& entry = BITS_LUT[i];
    if (merged_sample_caps_.pcm_size_rate_ & entry.flag) {
      printf(" %u", entry.bits);
    }
  }
  printf("\n");

  DumpAmpCaps(conv_, "Conv");
  DumpAmpCaps(pc_, "PC");

  if (pc_.pin_caps.can_pres_detect()) {
    printf("Plug Detect       : %s (current state %s)\n",
           pc_.async_plug_det ? "Asynchronous" : "Poll-only",
           plug_state_ ? "Plugged" : "Unplugged");
  } else {
    printf("Plug Detect       : No\n");
  }
}

void HdmiStream::DumpAmpCaps(const CommonCaps& caps, const char* tag) {
  if (caps.has_amp) {
    printf("%4s Gain control : [%.2f, %.2f] dB in %.2f dB steps (%s mute).\n", tag, caps.min_gain,
           caps.max_gain, caps.gain_step, caps.amp_caps.can_mute() ? "can" : "cannot");
  } else {
    printf("%4s Gain control : 0dB fixed (cannot mute)\n", tag);
  }
}

zx_status_t HdmiStream::OnActivateLocked() {
  // Start by attempting to put our pin complex and converter into a disabled
  // state.
  zx_status_t res = DisableConverterLocked();
  if (res != ZX_OK) {
    return res;
  }

  // Start the setup process by fetching the widget caps for our converter and
  // pin complex.  This will let us know where various parameters (sample
  // size/rate, stream format, amplifier caps, etc...) come from.  Also, go
  // ahead and fetch the pin caps so we have an idea of our presence detection
  // capabilities.
  const Command SETUP[] = {
      {props_.pc_nid, GET_PARAM(CodecParam::AW_CAPS), THUNK(ProcessPinWidgetCaps)},
      {props_.pc_nid, GET_CONFIG_DEFAULT, THUNK(ProcessPinCfgDefaults)},
      {props_.pc_nid, GET_PARAM(CodecParam::PIN_CAPS), THUNK(ProcessPinCaps)},
      {props_.conv_nid, GET_PARAM(CodecParam::AW_CAPS), THUNK(ProcessConverterWidgetCaps)},
  };

  return RunCmdListLocked(SETUP, std::size(SETUP));
}

zx_status_t HdmiStream::ProcessPinWidgetCaps(const Command& cmd, const CodecResponse& resp) {
  // Stash the pin's audio-widget caps.  We will need it while processing the
  // pin caps to determine if we need to register for async plug detection
  // notifications before querying the initial pin state.
  pc_.widget_caps.raw_data_ = resp.data;

  // Does this pin complex have an amplifier?  If so, we need to query what
  // it's caps, so we know what it's mute capabilities and unity gain are.  If
  // not, we are done.
  pc_.has_amp = pc_.widget_caps.output_amp_present();

  if (!pc_.has_amp) {
    return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);
  }

  return RunCmdLocked({pc_.widget_caps.amp_param_override() ? props_.pc_nid : props_.afg_nid,
                       GET_PARAM(AMP_CAPS(false)), THUNK(ProcessPinAmpCaps)});
}

zx_status_t HdmiStream::ProcessPinAmpCaps(const Command& cmd, const CodecResponse& resp) {
  pc_.amp_caps.raw_data_ = resp.data;

  pc_.gain_step = pc_.amp_caps.step_size_db();
  pc_.min_gain = pc_.amp_caps.min_gain_db();
  pc_.max_gain = pc_.amp_caps.max_gain_db();

  return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);
}

zx_status_t HdmiStream::ProcessPinCfgDefaults(const Command& cmd, const CodecResponse& resp) {
  pc_.cfg_defaults.raw_data_ = resp.data;
  return ZX_OK;
}

zx_status_t HdmiStream::ProcessPinCaps(const Command& cmd, const CodecResponse& resp) {
  pc_.pin_caps.raw_data_ = resp.data;

  // Sanity check out input/output configuration.
  if (!pc_.pin_caps.can_output()) {
    zxlogf(ERROR, "Output stream, but pin complex cannot output");
    return ZX_ERR_BAD_STATE;
  }

  // Is the Jack Detect Override bit set in our config defaults?  If so,
  // force-clear all of the bits in the pin caps which indicate an ability to
  // perform presence detection and impedence sensing.  Even though hardware
  // technically has the ability to perform presence detection, the
  // BIOS/Device manufacturer is trying to tell us that presence detection
  // circuitry has not been wired up, and that this stream is hardwired.
  //
  if (pc_.cfg_defaults.jack_detect_override()) {
    static constexpr uint32_t mask = AW_PIN_CAPS_FLAG_CAN_IMPEDANCE_SENSE |
                                     AW_PIN_CAPS_FLAG_TRIGGER_REQUIRED |
                                     AW_PIN_CAPS_FLAG_CAN_PRESENCE_DETECT;
    pc_.pin_caps.raw_data_ &= ~mask;
  }

  // Can this stream determine if it is connected or not?  If not, then we
  // just assume that we are always plugged in.
  if (!pc_.pin_caps.can_pres_detect() || pc_.pin_caps.trig_required()) {
    if (pc_.pin_caps.trig_required()) {
      zxlogf(WARNING,
             "Triggered impedence sense plug detect not supported.  "
             "Stream will always appear to be plugged in.");
    }
    return UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
  }

  // Looks like we support presence detection.  Enable unsolicited
  // notifications of pin state if supported, then query the initial pin
  // state.
  pc_.async_plug_det = pc_.widget_caps.can_send_unsol();
  if (pc_.async_plug_det) {
    zx_status_t res = AllocateUnsolTagLocked(&pc_.unsol_tag);
    if (res == ZX_OK) {
      zx_status_t res =
          RunCmdLocked({props_.pc_nid, SET_UNSOLICITED_RESP_CTRL(true, pc_.unsol_tag)});
      if (res != ZX_OK) {
        return res;
      }
    } else {
      zxlogf(WARNING,
             "Failed to allocate unsolicited response tag from "
             "codec pool (res %d).  Asynchronous plug detection will be "
             "disabled.",
             res);
      pc_.async_plug_det = false;
    }
  }

  // Now that notifications have been enabled (or not), query the initial pin state.
  return RunCmdLocked({props_.pc_nid, GET_PIN_SENSE, THUNK(ProcessPinState)});
}

zx_status_t HdmiStream::ProcessPinState(const Command& cmd, const CodecResponse& resp) {
  plug_state_ = PinSenseState(resp.data).presence_detect();
  last_plug_time_ = zx::clock::get_monotonic().get();
  return UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
}

zx_status_t HdmiStream::ProcessConverterWidgetCaps(const Command& cmd, const CodecResponse& resp) {
  zx_status_t res;

  conv_.widget_caps.raw_data_ = resp.data;
  conv_.has_amp = conv_.widget_caps.output_amp_present();

  // Fetch the amp caps (if any) either from the converter or the defaults
  // from the function group if the converter has not overridden them.
  if (conv_.has_amp) {
    uint16_t nid = conv_.widget_caps.amp_param_override() ? props_.conv_nid : props_.afg_nid;
    res = RunCmdLocked({nid, GET_PARAM(AMP_CAPS(false)), THUNK(ProcessConverterAmpCaps)});
    if (res != ZX_OK)
      return res;
  }

  // Fetch the supported sample rates, bit depth, and formats.
  uint16_t nid = conv_.widget_caps.format_override() ? props_.conv_nid : props_.afg_nid;
  const Command FETCH_FORMATS[] = {
      {nid, GET_PARAM(CodecParam::SUPPORTED_PCM_SIZE_RATE), THUNK(ProcessConverterSampleSizeRate)},
      {nid, GET_PARAM(CodecParam::SUPPORTED_STREAM_FORMATS), THUNK(ProcessConverterSampleFormats)},
  };

  res = RunCmdListLocked(FETCH_FORMATS, std::size(FETCH_FORMATS));
  if (res != ZX_OK) {
    return res;
  }

  return ZX_OK;
}

zx_status_t HdmiStream::ProcessConverterAmpCaps(const Command& cmd, const CodecResponse& resp) {
  conv_.amp_caps.raw_data_ = resp.data;

  // We support gain in case there is HDMI HW exposning it, but have not seen it yet.
  conv_.gain_step = conv_.amp_caps.step_size_db();
  conv_.min_gain = conv_.amp_caps.min_gain_db();
  conv_.max_gain = conv_.amp_caps.max_gain_db();

  return UpdateConverterGainLocked(std::max(props_.default_conv_gain, conv_.min_gain));
}

zx_status_t HdmiStream::ProcessConverterSampleSizeRate(const Command& cmd,
                                                       const CodecResponse& resp) {
  conv_.sample_caps.pcm_size_rate_ = resp.data;
  return UpdateSetupProgressLocked(SAMPLE_SIZE_RATE_COMPLETE);
}

zx_status_t HdmiStream::ProcessConverterSampleFormats(const Command& cmd,
                                                      const CodecResponse& resp) {
  conv_.sample_caps.pcm_formats_ = resp.data;
  return UpdateSetupProgressLocked(SAMPLE_FORMATS_COMPLETE);
}

zx_status_t HdmiStream::ProcessDataIslandPacketSizeInfo(const Command& cmd,
                                                        const CodecResponse& resp) {
  if (!eld_valid_) {
    zxlogf(ERROR, "Process ELD while ELD valid state is false");
    return ZX_ERR_BAD_STATE;
  }
  eld_size_ = static_cast<uint8_t>(resp.data);  // Section 7.3.3.36.
  if (eld_size_ == 0) {
    zxlogf(ERROR, "Received ELD size zero, invalid");
    return ZX_ERR_BAD_STATE;
  }

  eld_data_ = fbl::Array<uint8_t>(new uint8_t[eld_size_], eld_size_);

  return RunCmdLocked({props_.pc_nid, GET_EDID_LIKE_DATA(eld_index_), THUNK(ProcessEld)});
}

zx_status_t HdmiStream::ProcessEld(const Command& cmd, const CodecResponse& resp) {
  if (!eld_valid_) {
    zxlogf(ERROR, "Process ELD while ELD valid state is false");
    return ZX_ERR_BAD_STATE;
  }
  if (eld_index_ >= eld_size_) {
    zxlogf(ERROR, "Process ELD with invalid index");
    return ZX_ERR_BAD_STATE;
  }
  eld_data_[eld_index_++] = resp.data & 0xff;
  if (eld_index_ != eld_size_) {
    return RunCmdLocked({props_.pc_nid, GET_EDID_LIKE_DATA(eld_index_), THUNK(ProcessEld)});
  } else {
    if (eld_index_ < sizeof(hda::EldHeader) + sizeof(hda::EldBaselinePart1)) {
      zxlogf(ERROR, "Malformed ELD, too small for the header and baseline part1");
      return ZX_ERR_BAD_STATE;
    }

    hda::EldBaselinePart1* part1 =
        reinterpret_cast<hda::EldBaselinePart1*>(eld_data_.get() + sizeof(hda::EldHeader));
    if (eld_index_ < sizeof(hda::EldHeader) + sizeof(hda::EldBaselinePart1) + part1->mnl() +
                         part1->sad_count() * sizeof(edid::ShortAudioDescriptor)) {
      zxlogf(ERROR, "Malformed ELD, too small for header and baseline");
      return ZX_ERR_BAD_STATE;
    }

    uint8_t* part2 = eld_data_.get() + sizeof(hda::EldHeader) + sizeof(hda::EldBaselinePart1);
    if (part1->mnl() > StreamProperties::kMaxValidMonitorNameLength) {
      zxlogf(ERROR, "ELD monitor name string length using reserved length");
      return ZX_ERR_BAD_STATE;
    }
    // There is no null termination in the ELD, we add it here to product_name.
    static_assert(sizeof(props_.product_name) >= StreamProperties::kMaxValidMonitorNameLength + 1);
    memcpy(props_.product_name, part2, part1->mnl());
    props_.product_name[part1->mnl()] = '\0';

    props_.mfr_name = edid::GetEisaVendorName(part1->manufacturer_name);

    // Check ELD for supported rates and formats common to the HDA controller and the HDMI HW.
    merged_sample_caps_ = {};
    edid::ShortAudioDescriptor* sad_list = reinterpret_cast<edid::ShortAudioDescriptor*>(
        eld_data_.get() + sizeof(hda::EldHeader) + sizeof(hda::EldBaselinePart1) + part1->mnl());
    zx_status_t status =
        MakeNewSampleCaps(conv_.sample_caps, sad_list, part1->sad_count(), merged_sample_caps_);
    if (status != ZX_OK) {
      zxlogf(WARNING, "Could not merge sample capabilities (res %d)", status);
      return status;
    }

    // We create a unique id, from 'HDMI' + port id + device ids.
    SetPersistentUniqueIdLocked(
        {'H', 'D', 'M', 'I', static_cast<uint8_t>(part1->port_id >> 0),
         static_cast<uint8_t>(part1->port_id >> 8), static_cast<uint8_t>(part1->port_id >> 16),
         static_cast<uint8_t>(part1->port_id >> 24), static_cast<uint8_t>(part1->port_id >> 32),
         static_cast<uint8_t>(part1->port_id >> 40), static_cast<uint8_t>(part1->port_id >> 48),
         static_cast<uint8_t>(part1->port_id >> 56),
         static_cast<uint8_t>(part1->manufacturer_name >> 0),
         static_cast<uint8_t>(part1->manufacturer_name >> 8),
         static_cast<uint8_t>(part1->product_code >> 0),
         static_cast<uint8_t>(part1->product_code >> 8)});

    // We were successful in getting a new sample capabilities by merging with the Short Audio
    // Descriptors from the ELD.
    return UpdateSetupProgressLocked(ELD_SETUP_COMPLETE);
  }
}
#undef THUNK

}  // namespace audio::intel_hda::codecs
