// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "realtek-stream.h"

#include <lib/zx/clock.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/vector.h>
#include <intel-hda/utils/codec-caps.h>
#include <intel-hda/utils/codec-commands.h>
#include <intel-hda/utils/codec-state.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"

DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(audio::intel_hda::codecs::RealtekStream::PCAT, 16);

namespace audio {
namespace intel_hda {
namespace codecs {

RealtekStream::RealtekStream(const StreamProperties& props)
    : IntelHDAStreamConfigBase(props.stream_id, props.is_input), props_(props) {
  SetPersistentUniqueId(props.uid);
}

zx_status_t RealtekStream::DisableConverterLocked(bool force_all) {
  const Command DISABLE_CONVERTER_VERBS[] = {
      {props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input())},
      {props_.pc_nid, SET_AMPLIFIER_GAIN_MUTE(true, 0, is_input(), !is_input())},
      {props_.conv_nid, SET_CONVERTER_STREAM_CHAN(IHDA_INVALID_STREAM_TAG, 0)},
      {props_.conv_nid, SET_POWER_STATE(HDA_PS_D3HOT)},
      {props_.pc_nid, SET_POWER_STATE(HDA_PS_D3HOT)},
  };

  return RunCmdListLocked(DISABLE_CONVERTER_VERBS, std::size(DISABLE_CONVERTER_VERBS), force_all);
}

zx_status_t RealtekStream::UpdateConverterGainLocked(float target_gain) {
  if (!conv_.has_amp)
    return ZX_ERR_NOT_SUPPORTED;

  if ((target_gain < conv_.min_gain) || (target_gain > conv_.max_gain))
    return ZX_ERR_INVALID_ARGS;

  ZX_DEBUG_ASSERT(conv_.gain_step > 0);

  float tmp = ((target_gain - conv_.min_gain) + (conv_.gain_step / 2)) / conv_.gain_step;
  ZX_DEBUG_ASSERT(static_cast<uint32_t>(tmp) <= conv_.amp_caps.num_steps());

  cur_conv_gain_steps_ = ComputeGainSteps(conv_, target_gain);
  return ZX_OK;
}

float RealtekStream::ComputeCurrentGainLocked() {
  return conv_.has_amp ? conv_.min_gain + (cur_conv_gain_steps_ * conv_.gain_step) : 0.0f;
}

zx_status_t RealtekStream::SendGainUpdatesLocked() {
  zx_status_t res;

  if (conv_.has_amp) {
    bool mute = conv_.amp_caps.can_mute() ? cur_mute_ : false;
    res = RunCmdLocked({props_.conv_nid, SET_AMPLIFIER_GAIN_MUTE(mute, cur_conv_gain_steps_,
                                                                 is_input(), !is_input())});
    if (res != ZX_OK)
      return res;
  }

  if (pc_.has_amp) {
    bool mute = pc_.amp_caps.can_mute() ? cur_mute_ : false;
    res = RunCmdLocked({props_.pc_nid, SET_AMPLIFIER_GAIN_MUTE(mute, cur_pc_gain_steps_, is_input(),
                                                               !is_input())});
    if (res != ZX_OK)
      return res;
  }

  return ZX_OK;
}

// static
uint8_t RealtekStream::ComputeGainSteps(const CommonCaps& caps, float target_gain) {
  if (!caps.has_amp || !caps.amp_caps.num_steps())
    return 0;

  if (target_gain < caps.min_gain)
    return 0;

  if (target_gain > caps.max_gain)
    return static_cast<uint8_t>(caps.amp_caps.num_steps() - 1);

  ZX_DEBUG_ASSERT(caps.gain_step > 0);
  float tmp = ((target_gain - caps.min_gain) + (caps.gain_step / 2)) / caps.gain_step;
  ZX_DEBUG_ASSERT(static_cast<uint32_t>(tmp) < caps.amp_caps.num_steps());

  return static_cast<uint8_t>(tmp);
}

zx_status_t RealtekStream::RunCmdLocked(const Command& cmd) {
  std::unique_ptr<PendingCommand> pending_cmd;
  bool want_response = (cmd.thunk != nullptr);

  if (want_response) {
    pending_cmd = PendingCommandAllocator::New(cmd);
    if (pending_cmd == nullptr)
      return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = SendCodecCommandLocked(cmd.nid, cmd.verb, want_response ? Ack::YES : Ack::NO);
  VERBOSE_LOG("SEND: nid %2hu verb 0x%05x%s", cmd.nid, cmd.verb.val, want_response ? "*" : "");

  if ((res == ZX_OK) && want_response)
    pending_cmds_.push_back(std::move(pending_cmd));

  if (cmd.delay_ms)
    zx::nanosleep(zx::deadline_after(zx::msec(cmd.delay_ms)));

  return res;
}

zx_status_t RealtekStream::RunCmdListLocked(const Command* list, size_t count, bool force_all) {
  ZX_DEBUG_ASSERT(list);

  zx_status_t total_res = ZX_OK;
  for (size_t i = 0; i < count; ++i) {
    zx_status_t res = RunCmdLocked(list[i]);

    if (res != ZX_OK) {
      if (!force_all)
        return res;

      if (total_res == ZX_OK)
        total_res = res;
    }
  }

  return total_res;
}

void RealtekStream::OnDeactivateLocked() { DisableConverterLocked(true); }

void RealtekStream::OnChannelDeactivateLocked(const StreamChannel& channel) {}

zx_status_t RealtekStream::OnDMAAssignedLocked() {
  return UpdateSetupProgressLocked(DMA_ASSIGNMENT_COMPLETE);
}

zx_status_t RealtekStream::OnSolicitedResponseLocked(const CodecResponse& resp) {
  if (pending_cmds_.is_empty()) {
    LOG("Received solicited response (0x%08x), but no commands are pending!", resp.data);
    return ZX_ERR_BAD_STATE;
  }

  auto pending_cmd = pending_cmds_.pop_front();
  VERBOSE_LOG("RECV: nid %2hu verb 0x%05x --> 0x%08x", pending_cmd->cmd().nid,
              pending_cmd->cmd().verb.val, resp.data);
  return pending_cmd->Invoke(this, resp);
}

zx_status_t RealtekStream::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
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
    last_plug_time_ = zx::clock::get_monotonic().get();

    std::optional<zx_status_t> fixup = PlugFixupsLocked();
    if (fixup)
      return *fixup;  // The fixup code will take care of notifies.

    // Inform anyone who has registered for notification.
    ZX_DEBUG_ASSERT(pc_.async_plug_det);
    NotifyPlugStateLocked(plug_state_, last_plug_time_);
  }

  return ZX_OK;
}

zx_status_t RealtekStream::BeginChangeStreamFormatLocked(const audio_proto::StreamSetFmtReq& fmt) {
  // Check the format arguments.
  //
  // Note: in the limited number of Realtek codecs I have seen so far, the
  // channel count given by a converter's widget caps is *the* number of
  // channels supported, not a maximum number of channels supported (as
  // indicated by the Intel HDA specification).  One can configure the number
  // of channels in the format specifier to be less than the number maximum
  // number of channels supported  by the converter, but it will ignore you.
  //
  // For inputs, configuring a stereo input converter for mono will cause the
  // converter to produce stereo frames anyway.  The controller side DMA
  // engine also does not seem smart enough to discard the extra sample (even
  // though it was configured for mono as well) and you will end up capturing
  // data an twice the rate you expected.
  //
  // For output, configuring a stereo output converter for mono seems to have
  // no real effect on its behavior.  It is still expecting stereo frames.
  // When you configure the DMA engine for mono (as is the requirement given
  // by Intel), the converter appears to be unhappy about the lack of samples
  // in the frame and simply never produces any output.  The Converter Channel
  // Count control (section 7.3.3.35 of the Intel HDA spec) also appears to
  // have no effect.  This is not particularly surprising as it is supposed to
  // only effect output converters, and only those with support for more than
  // 2 channels, but I tried it anyway.
  //
  // Perhaps this is different for the 6xx series of codecs from Realtek (the
  // 6 channel "surround sound ready" codecs); so far I have only worked with
  // samples from the 2xx series (the stereo codec family).  For now, however,
  // insist that the format specified by the user exactly match the number of
  // channels present in the converter we are using for this pipeline.
  if (!fmt.channels || (fmt.channels != conv_.widget_caps.ch_count()))
    return ZX_ERR_NOT_SUPPORTED;

  if (!conv_.sample_caps.SupportsRate(fmt.frames_per_second) ||
      !conv_.sample_caps.SupportsFormat(fmt.sample_format))
    return ZX_ERR_NOT_SUPPORTED;

  // Looks good, make sure that the converter is muted and not processing any stream tags.
  format_set_ = false;
  return DisableConverterLocked();
}

zx_status_t RealtekStream::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  zx_status_t res;
  const Command ENABLE_CONVERTER_VERBS[] = {
      {props_.conv_nid, SET_CONVERTER_FORMAT(encoded_fmt)},
      {props_.conv_nid, SET_CONVERTER_STREAM_CHAN(dma_stream_tag(), 0)},
      {props_.pc_nid, SET_POWER_STATE(HDA_PS_D0)},
      {props_.conv_nid, SET_POWER_STATE(HDA_PS_D0)},
      {props_.pc_nid,
       SET_ANALOG_PIN_WIDGET_CTRL(!is_input(), is_input(), pc_.pin_caps.can_drive_headphones())},
  };

  res = RunCmdListLocked(ENABLE_CONVERTER_VERBS, std::size(ENABLE_CONVERTER_VERBS));
  if (res != ZX_OK)
    return res;

  res = SendGainUpdatesLocked();
  if (res != ZX_OK)
    return res;

  format_set_ = true;
  return ZX_OK;
}

void RealtekStream::OnGetGainLocked(audio_proto::GainState* out_resp) {
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

void RealtekStream::OnSetGainLocked(const audio_proto::SetGainReq& req,
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

    if ((res == ZX_OK) && set_gain)
      res = UpdateConverterGainLocked(req.gain);
  }

  if (res == ZX_OK) {
    cur_mute_ = mute_target;

    // Don't bother sending any update to the converter if the format is not currently set.
    if (format_set_)
      res = SendGainUpdatesLocked();
  }

  if (out_resp != nullptr) {
    out_resp->result = res;
    out_resp->cur_mute = cur_mute_;
    out_resp->cur_gain = ComputeCurrentGainLocked();
  }
}

void RealtekStream::OnPlugDetectLocked(StreamChannel* response_channel,
                                       audio_proto::PlugDetectResp* out_resp) {
  ZX_DEBUG_ASSERT(response_channel != nullptr);

  // If our pin cannot perform presence detection, just fall back on the base class impl.
  if (!pc_.pin_caps.can_pres_detect()) {
    IntelHDAStreamConfigBase::OnPlugDetectLocked(response_channel, out_resp);
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
    // TODO(johngro): In order to do proper polling support, we need to add
    // the concept of a pending client request to the system.  IOW - we need
    // to create and run a state machine where we hold a reference to the
    // client's response channel, and eventually respond to the client using
    // the same transaction ID they requested state with.
    //
    // For now, if our hardware does not support async plug detect, we
    // simply fall back on the default implementation which reports that we
    // are hardwired and always plugged in.
    IntelHDAStreamConfigBase::OnPlugDetectLocked(response_channel, out_resp);
    return;
  }
}

void RealtekStream::OnGetStringLocked(const audio_proto::GetStringReq& req,
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
      IntelHDAStreamConfigBase::OnGetStringLocked(req, out_resp);
      return;
  }

  int res = snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "%s",
                     requested_string ? requested_string : "<unassigned>");
  ZX_DEBUG_ASSERT(res >= 0);
  out_resp->result = ZX_OK;
  out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
  out_resp->id = req.id;
}

zx_status_t RealtekStream::UpdateSetupProgressLocked(uint32_t stage) {
  ZX_DEBUG_ASSERT(!(setup_progress_ & STREAM_PUBLISHED));
  ZX_DEBUG_ASSERT(!(setup_progress_ & stage));

  setup_progress_ |= stage;

  if (setup_progress_ == ALL_SETUP_COMPLETE) {
    zx_status_t res = FinalizeSetupLocked();
    if (res != ZX_OK)
      return res;

    setup_progress_ |= STREAM_PUBLISHED;
    DumpStreamPublishedLocked();
    return PublishDeviceLocked();
  }

  return ZX_OK;
}

zx_status_t RealtekStream::FinalizeSetupLocked() {
  // Stash the number of gain steps to use in the pin converter.  This allows
  // us to hardcode gain targets for things like mic boost.  Eventually, we
  // need to expose a way to detect this capability and control it via APIs,
  // but for now we can get away with just setting it as part of the finalize
  // step for setup.
  cur_pc_gain_steps_ = ComputeGainSteps(pc_, props_.default_pc_gain);

  // Compute the list of formats we support.
  fbl::Vector<audio_proto::FormatRange> supported_formats;
  zx_status_t res =
      MakeFormatRangeList(conv_.sample_caps, conv_.widget_caps.ch_count(), &supported_formats);
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to compute supported format ranges!  (res = %d)", res);
    return res;
  }

  // At this point, we should have at least one sample encoding that we
  // support.  If we don't, then this output stream is pretty worthless.
  if (!supported_formats.size()) {
    DEBUG_LOG(
        "WARNING - no sample encodings are supported by this audio stream!  "
        "(formats = 0x%08x, size/rates = 0x%08x)",
        conv_.sample_caps.pcm_formats_, conv_.sample_caps.pcm_size_rate_);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Go over the list of format ranges produced and tweak it to account for
  // seemingly non-standard Realtek codec behavior.  Usually, when a converter
  // says that it supports a maximum of N channels, you are supposed to be
  // able to configure it for any number of channels in the set [1, N].  The
  // Realtek codecs I have encountered so far, however, only support the
  // number of channels they claim to support.  IOW - If the converter says
  // that max_channels == 2, and you configure it for 1 channel, it will still
  // produce 2 audio frames per frame period.
  for (auto& format : supported_formats)
    format.min_channels = format.max_channels;

  SetSupportedFormatsLocked(std::move(supported_formats));

  return ZX_OK;
}

void RealtekStream::DumpStreamPublishedLocked() {
  if (!DEBUG_LOGGING)
    return;

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

  LOG("Setup complete, publishing %s stream", props_.is_input ? "input" : "output");
  LOG("Channels          : %u", conv_.widget_caps.ch_count());

  LOG("Sample rates      :");
  for (size_t i = 0; i < std::size(RATE_LUT); ++i) {
    const auto& entry = RATE_LUT[i];
    if (conv_.sample_caps.pcm_size_rate_ & entry.flag)
      printf(" %u", entry.rate);
  }
  printf("\n");

  LOG("Sample bits       :");
  for (size_t i = 0; i < std::size(BITS_LUT); ++i) {
    const auto& entry = BITS_LUT[i];
    if (conv_.sample_caps.pcm_size_rate_ & entry.flag)
      printf(" %u", entry.bits);
  }
  printf("\n");

  DumpAmpCaps(conv_, "Conv");
  DumpAmpCaps(pc_, "PC");

  if (pc_.pin_caps.can_pres_detect()) {
    LOG("Plug Detect       : %s (current state %s)",
        pc_.async_plug_det ? "Asynchronous" : "Poll-only", plug_state_ ? "Plugged" : "Unplugged");
  } else {
    LOG("Plug Detect       : No");
  }
}

void RealtekStream::DumpAmpCaps(const CommonCaps& caps, const char* tag) {
  if (caps.has_amp) {
    LOG("%4s Gain control : [%.2f, %.2f] dB in %.2f dB steps (%s mute).", tag, caps.min_gain,
        caps.max_gain, caps.gain_step, caps.amp_caps.can_mute() ? "can" : "cannot");
  } else {
    LOG("%4s Gain control : 0dB fixed (cannot mute)", tag);
  }
}

#define THUNK(_method) (&RealtekStream::_method)

#define SET_ONE_PROCESSING_COEFFICIENT(nid, coeff_index, value) \
  {nid, SET_COEFFICIENT_INDEX(coeff_index)}, { nid, SET_PROCESSING_COEFFICIENT(value) }

#define SET_ONE_PROCESSING_COEFFICIENT_AND_DELAY_MS(nid, coeff_index, value, delay) \
  {nid, SET_COEFFICIENT_INDEX(coeff_index)}, {                                      \
    nid, SET_PROCESSING_COEFFICIENT(value), nullptr, delay                          \
  }

// Called on stream activation.
zx_status_t RealtekStream::ActivateFixupsLocked() {
  for (const auto fixup_id : props_.fixups) {
    switch (fixup_id) {
      case FIXUP_DELL1_HEADSET: {
        const Command FIXUP[] = {
            // Set External Amplifier Power Down to verb control.
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x10, 0x20),

            // Set headset jack to defaults.  This appears to be
            // a similar configuration to the one used for probing below.
            // Processing Coefficient 0x45: 0xD089, used for probe
            // Processing Coefficient 0x45: 0xD489, CTIA headset
            // Processing Coefficient 0x45: 0xE489, OMTP headset
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x1B, 0x884B),
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x45, 0xD089),
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x1B, 0x84B),
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x46, 0x4),
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x1B, 0xC4B),

            // Other undocumented magic.
            SET_ONE_PROCESSING_COEFFICIENT(87, 0x4, 0x8229),
            SET_ONE_PROCESSING_COEFFICIENT(32, 0x46, 0xF4),
            SET_ONE_PROCESSING_COEFFICIENT(87, 0x4, 0x822C),
            SET_ONE_PROCESSING_COEFFICIENT(83, 0x2, 0x8000),
            SET_ONE_PROCESSING_COEFFICIENT(83, 0x2, 0x0),

            // Configure Realtek PC Beep Hidden Register, then
            // 50 millisecond delay to let the headphone jack
            // pin state settle down; without this delay, the
            // initial pin state polling may return unplugged
            // when the headset is actually plugged in.
            SET_ONE_PROCESSING_COEFFICIENT_AND_DELAY_MS(32, 0x36, 0x5757, 50),
        };

        zx_status_t res = RunCmdListLocked(FIXUP, std::size(FIXUP));
        if (res != ZX_OK)
          return res;
      } break;
      default:
        LOG("ERROR: Unknown fixup: %d", static_cast<int>(fixup_id));
        return ZX_ERR_NOT_SUPPORTED;
    }
  }

  return ZX_OK;
}

zx_status_t RealtekStream::DellHeadsetPreProbeLocked() {
  const Command PREPROBE[] = {
      // Undocumented -- configure for headset probing.
      SET_ONE_PROCESSING_COEFFICIENT(32, 0x1B, 0xE4B),
      SET_ONE_PROCESSING_COEFFICIENT(32, 0x6, 0x6104),
      SET_ONE_PROCESSING_COEFFICIENT(87, 0x3, 0x9A3),

      // Mute output for the duration of the probe.
      {33u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true, 0), nullptr, /*delay_ms=*/80},
      {33u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false)},
      SET_ONE_PROCESSING_COEFFICIENT(32, 0x45, 0xD089),

      // This appears to trigger the actual probe.
      SET_ONE_PROCESSING_COEFFICIENT_AND_DELAY_MS(32, 0x49, 0x149, 300),

      // Trigger a read of the probe result.
      {32u, SET_COEFFICIENT_INDEX(0x46)},
      {32u, GET_PROCESSING_COEFFICIENT, THUNK(DellHeadsetProbeResponse)},
  };

  return RunCmdListLocked(PREPROBE, std::size(PREPROBE));
}

zx_status_t RealtekStream::DellHeadsetProbeResponse(const Command& cmd, const CodecResponse& resp) {
  // Save result of the probe.
  headset_is_ctia_ = ((resp.data & 0xF0) == 0xF0);

  const Command AFTER_PROBE[] = {
      SET_ONE_PROCESSING_COEFFICIENT(87, 0x3, 0xDA3),
      {87u, SET_COEFFICIENT_INDEX(0x5)},
      {87u, GET_PROCESSING_COEFFICIENT, THUNK(DellHeadsetProbeFinish)},
  };

  return RunCmdListLocked(AFTER_PROBE, std::size(AFTER_PROBE));
}

zx_status_t RealtekStream::DellHeadsetProbeFinish(const Command& cmd, const CodecResponse& resp) {
  const Command FINISH[] = {
      // This processing coefficient change appears to configure the
      // headphone output amplifier to verb control.
      SET_ONE_PROCESSING_COEFFICIENT(87, 0x5, (uint16_t)(resp.data & ~(1 << 14))),
      {33u, SET_ANALOG_PIN_WIDGET_CTRL(true, false, false), nullptr, /*delay_ms=*/80},

      // Unmute after test
      {33u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0)},

      // Configure the headset type.
      SET_ONE_PROCESSING_COEFFICIENT(32, 0x45, headset_is_ctia_ ? 0xD489 : 0xE489),
      SET_ONE_PROCESSING_COEFFICIENT(32, 0x1B, 0xE6B),
  };

  zx_status_t res = RunCmdListLocked(FINISH, std::size(FINISH));
  if (res != ZX_OK)
    return res;

  // Headset programming is done.
  // We need to inform anybody that cares.
  if (!(setup_progress_ & PLUG_STATE_SETUP_COMPLETE)) {
    // We're running during initial setup, inform the state machine
    // that plug state setup is done.
    UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
  }
  NotifyPlugStateLocked(plug_state_, last_plug_time_);

  return ZX_OK;
}

// Called after any change in the plug state.
// Returns nullopt if nothing was done, or if any required action
// was fully completed.  Returns ZX_OK to indicate that the fixup
// has taken over the state machine; the caller is relieved of
// any further responsibility.  Error returns represent real errors
// that should be reported.
std::optional<zx_status_t> RealtekStream::PlugFixupsLocked() {
  for (const auto fixup_id : props_.fixups) {
    switch (fixup_id) {
      case FIXUP_DELL1_HEADSET:
        if (plug_state_) {
          // If something is plugged in, hand over control
          // to the headset probe state machine.
          return DellHeadsetPreProbeLocked();
        }
        break;
      default:
        break;
    }
  }
  return std::nullopt;
}

zx_status_t RealtekStream::OnActivateLocked() {
  // Start by attempting to put our pin complex and converter into a disabled
  // state.
  zx_status_t res = DisableConverterLocked();
  if (res != ZX_OK)
    return res;

  // Run fixups if needed.
  res = ActivateFixupsLocked();
  if (res != ZX_OK)
    return res;

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

zx_status_t RealtekStream::ProcessPinWidgetCaps(const Command& cmd, const CodecResponse& resp) {
  // Stash the pin's audio-widget caps.  We will need it while processing the
  // pin caps to determine if we need to register for async plug detection
  // notifications before querying the initial pin state.
  pc_.widget_caps.raw_data_ = resp.data;

  // Does this pin complex have an amplifier?  If so, we need to query what
  // it's caps, so we know what it's mute capabilities and unity gain are.  If
  // not, we are done.
  pc_.has_amp =
      is_input() ? pc_.widget_caps.input_amp_present() : pc_.widget_caps.output_amp_present();

  if (!pc_.has_amp)
    return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);

  return RunCmdLocked({pc_.widget_caps.amp_param_override() ? props_.pc_nid : props_.afg_nid,
                       GET_PARAM(AMP_CAPS(is_input())), THUNK(ProcessPinAmpCaps)});
}

zx_status_t RealtekStream::ProcessPinAmpCaps(const Command& cmd, const CodecResponse& resp) {
  pc_.amp_caps.raw_data_ = resp.data;

  pc_.gain_step = pc_.amp_caps.step_size_db();
  pc_.min_gain = pc_.amp_caps.min_gain_db();
  pc_.max_gain = pc_.amp_caps.max_gain_db();

  return UpdateSetupProgressLocked(PIN_COMPLEX_SETUP_COMPLETE);
}

zx_status_t RealtekStream::ProcessPinCfgDefaults(const Command& cmd, const CodecResponse& resp) {
  pc_.cfg_defaults.raw_data_ = resp.data;
  return ZX_OK;
}

zx_status_t RealtekStream::ProcessPinCaps(const Command& cmd, const CodecResponse& resp) {
  pc_.pin_caps.raw_data_ = resp.data;

  // Sanity check out input/output configuration.
  if (!(is_input() ? pc_.pin_caps.can_input() : pc_.pin_caps.can_output())) {
    const char* tag = is_input() ? "input" : "output";
    LOG("ERROR: Stream configured for %s, but pin complex cannot %s", tag, tag);
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
      LOG("WARNING : Triggered impedence sense plug detect not supported.  "
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
      if (res != ZX_OK)
        return res;
    } else {
      LOG("WARNING : Failed to allocate unsolicited response tag from "
          "codec pool (res %d).  Asynchronous plug detection will be "
          "disabled.",
          res);
      pc_.async_plug_det = false;
    }
  }

  // Now that notifications have been enabled (or not), query the initial pin state.
  return RunCmdLocked({props_.pc_nid, GET_PIN_SENSE, THUNK(ProcessPinState)});
}

zx_status_t RealtekStream::ProcessPinState(const Command& cmd, const CodecResponse& resp) {
  plug_state_ = PinSenseState(resp.data).presence_detect();
  last_plug_time_ = zx::clock::get_monotonic().get();

  std::optional<zx_status_t> fixup = PlugFixupsLocked();
  if (fixup)
    return *fixup;  // The fixup code will update the setup progress.

  return UpdateSetupProgressLocked(PLUG_STATE_SETUP_COMPLETE);
}

zx_status_t RealtekStream::ProcessConverterWidgetCaps(const Command& cmd,
                                                      const CodecResponse& resp) {
  zx_status_t res;

  conv_.widget_caps.raw_data_ = resp.data;
  conv_.has_amp =
      is_input() ? conv_.widget_caps.input_amp_present() : conv_.widget_caps.output_amp_present();

  // Fetch the amp caps (if any) either from the converter or the defaults
  // from the function group if the converter has not overridden them.
  if (conv_.has_amp) {
    uint16_t nid = conv_.widget_caps.amp_param_override() ? props_.conv_nid : props_.afg_nid;
    res = RunCmdLocked({nid, GET_PARAM(AMP_CAPS(is_input())), THUNK(ProcessConverterAmpCaps)});
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
  if (res != ZX_OK)
    return res;

  return ZX_OK;
}

zx_status_t RealtekStream::ProcessConverterAmpCaps(const Command& cmd, const CodecResponse& resp) {
  conv_.amp_caps.raw_data_ = resp.data;

  conv_.gain_step = conv_.amp_caps.step_size_db();
  conv_.min_gain = conv_.amp_caps.min_gain_db();
  conv_.max_gain = conv_.amp_caps.max_gain_db();

  return UpdateConverterGainLocked(std::max(props_.default_conv_gain, conv_.min_gain));
}

zx_status_t RealtekStream::ProcessConverterSampleSizeRate(const Command& cmd,
                                                          const CodecResponse& resp) {
  conv_.sample_caps.pcm_size_rate_ = resp.data;
  return ZX_OK;
}

zx_status_t RealtekStream::ProcessConverterSampleFormats(const Command& cmd,
                                                         const CodecResponse& resp) {
  conv_.sample_caps.pcm_formats_ = resp.data;
  return UpdateSetupProgressLocked(CONVERTER_SETUP_COMPLETE);
}
#undef THUNK

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
