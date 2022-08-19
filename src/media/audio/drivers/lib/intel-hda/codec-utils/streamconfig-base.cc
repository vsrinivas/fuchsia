// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/server.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <audio-proto/audio-proto.h>
#include <fbl/algorithm.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/streamconfig-base.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "debug-logging.h"

namespace {
namespace audio_fidl = fuchsia_hardware_audio;
}  // namespace

namespace audio::intel_hda::codecs {

zx_protocol_device_t IntelHDAStreamConfigBase::STREAM_DEVICE_THUNKS = []() {
  zx_protocol_device_t sdt = {};
  sdt.version = DEVICE_OPS_VERSION;
  sdt.message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    IntelHDAStreamConfigBase* thiz = static_cast<IntelHDAStreamConfigBase*>(ctx);
    DdkTransaction transaction(txn);
    fidl::WireDispatch<fuchsia_hardware_audio::StreamConfigConnector>(
        thiz, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
    return transaction.Status();
  };
  return sdt;
}();

IntelHDAStreamConfigBase::IntelHDAStreamConfigBase(uint32_t id, bool is_input)
    : IntelHDAStreamBase(id, is_input), loop_(&kAsyncLoopConfigNeverAttachToThread) {
  plug_time_ = zx::clock::get_monotonic().get();
  loop_.StartThread("intel-hda-stream-loop");
}

void IntelHDAStreamConfigBase::Connect(ConnectRequestView request,
                                       ConnectCompleter::Sync& completer) {
  fbl::AutoLock lock(obj_lock());

  // Do not allow any new connections if we are in the process of shutting down
  if (!is_active()) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  // For now, block new connections if we currently have no privileged
  // connection, but there is a SetFormat request in flight to the codec
  // driver.  We are trying to avoid the following sequence...
  //
  // 1) A privileged connection starts a set format.
  // 2) After we ask the controller to set the format, our privileged channel
  //    is closed.
  // 3) A new user connects.
  // 4) The response to the first client's request arrives and gets sent
  //    to the second client.
  // 5) Confusion ensues.
  //
  // Denying new connections while the old request is in flight avoids this,
  // but is generally a terrible solution.  What we should really do is tag
  // the requests to the codec driver with a unique ID which we can use to
  // filter responses.  One option might be to split the transaction ID so
  // that a portion of the TID is used for stream routing, while another
  // portion is used for requests like this.
  bool privileged = (stream_channel_ == nullptr);
  if (privileged && IsFormatChangeInProgress()) {
    completer.Close(ZX_ERR_SHOULD_WAIT);
    return;
  }

  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have a stream_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  fbl::RefPtr<StreamChannel> stream_channel = StreamChannel::Create(this);
  if (stream_channel == nullptr) {
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  stream_channels_.push_back(stream_channel);

  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::StreamConfig>> on_unbound =
      [this, stream_channel](fidl::WireServer<audio_fidl::StreamConfig>*, fidl::UnbindInfo,
                             fidl::ServerEnd<fuchsia_hardware_audio::StreamConfig>) {
        fbl::AutoLock channel_lock(this->obj_lock());
        this->ProcessClientDeactivateLocked(stream_channel.get());
      };

  fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfig>>(
      loop_.dispatcher(), std::move(request->protocol), stream_channel.get(),
      std::move(on_unbound));

  if (privileged) {
    stream_channel_ = stream_channel;
  }
}

void IntelHDAStreamConfigBase::GetSupportedFormats(
    StreamChannel::GetSupportedFormatsCompleter::Sync& completer) {
  if (supported_formats_.size() > std::numeric_limits<uint16_t>::max()) {
    LOG("Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
        supported_formats_.size());
    return;
  }

  // Build formats compatible with FIDL from a vector of audio_stream_format_range_t.
  // Needs to be alive until the reply is sent.
  struct FidlCompatibleFormats {
    fbl::Vector<uint8_t> number_of_channels;
    fbl::Vector<audio_fidl::wire::SampleFormat> sample_formats;
    fbl::Vector<uint32_t> frame_rates;
    fbl::Vector<uint8_t> valid_bits_per_sample;
    fbl::Vector<uint8_t> bytes_per_sample;
  };
  fbl::Vector<FidlCompatibleFormats> fidl_compatible_formats;
  for (auto& i : supported_formats_) {
    auto formats = audio::utils::GetAllFormats(i.sample_formats);
    ZX_ASSERT(formats.size() >= 1);
    for (auto& j : formats) {
      fbl::Vector<uint32_t> rates;
      // Ignore flags if min and max are equal.
      if (i.min_frames_per_second == i.max_frames_per_second) {
        rates.push_back(i.min_frames_per_second);
      } else {
        ZX_DEBUG_ASSERT(!(i.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS));
        audio::utils::FrameRateEnumerator enumerator(i);
        for (uint32_t rate : enumerator) {
          rates.push_back(rate);
        }
      }

      fbl::Vector<uint8_t> number_of_channels;
      for (uint8_t j = i.min_channels; j <= i.max_channels; ++j) {
        number_of_channels.push_back(j);
      }

      fidl_compatible_formats.push_back({
          std::move(number_of_channels),
          {j.format},
          std::move(rates),
          {j.valid_bits_per_sample},
          {j.bytes_per_sample},
      });
    }
  }

  fidl::Arena allocator;
  fidl::VectorView<audio_fidl::wire::SupportedFormats> fidl_formats(allocator,
                                                                    fidl_compatible_formats.size());
  for (size_t i = 0; i < fidl_compatible_formats.size(); ++i) {
    // Get FIDL PcmSupportedFormats from FIDL compatible vectors.
    // Needs to be alive until the reply is sent.
    FidlCompatibleFormats& src = fidl_compatible_formats[i];
    audio_fidl::wire::PcmSupportedFormats formats;

    fidl::VectorView<audio_fidl::wire::ChannelSet> channel_sets(allocator,
                                                                src.number_of_channels.size());

    for (uint8_t j = 0; j < src.number_of_channels.size(); ++j) {
      fidl::VectorView<audio_fidl::wire::ChannelAttributes> all_attributes(
          allocator, src.number_of_channels[j]);
      channel_sets[j].Allocate(allocator);
      channel_sets[j].set_attributes(allocator, std::move(all_attributes));
    }
    formats.Allocate(allocator);
    formats.set_channel_sets(allocator, std::move(channel_sets));
    formats.set_sample_formats(allocator,
                               ::fidl::VectorView<audio_fidl::wire::SampleFormat>::FromExternal(
                                   src.sample_formats.data(), src.sample_formats.size()));
    formats.set_frame_rates(allocator, ::fidl::VectorView<uint32_t>::FromExternal(
                                           src.frame_rates.data(), src.frame_rates.size()));
    formats.set_bytes_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.bytes_per_sample.data(),
                                                             src.bytes_per_sample.size()));
    formats.set_valid_bits_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.valid_bits_per_sample.data(),
                                                             src.valid_bits_per_sample.size()));
    fidl_formats[i].Allocate(allocator);
    fidl_formats[i].set_pcm_supported_formats(allocator, std::move(formats));
  }

  completer.Reply(std::move(fidl_formats));
}

void IntelHDAStreamConfigBase::CreateRingBuffer(
    StreamChannel* channel, audio_fidl::wire::Format format,
    ::fidl::ServerEnd<audio_fidl::RingBuffer> ring_buffer,
    StreamChannel::CreateRingBufferCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(channel != nullptr);
  zx_status_t res;

  // Only the privileged stream channel is allowed to change the format.
  if (channel != stream_channel_.get()) {
    LOG("Unprivileged channel cannot set the format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  bool found_one = false;
  auto format_pcm = format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      format_pcm.valid_bits_per_sample, 8 * format_pcm.bytes_per_sample);

  // Check the format for compatibility
  for (const auto& fmt : supported_formats_) {
    if (audio::utils::FormatIsCompatible(format_pcm.frame_rate,
                                         static_cast<uint16_t>(format_pcm.number_of_channels),
                                         sample_format, fmt)) {
      found_one = true;
      break;
    }
  }

  if (!found_one) {
    LOG("Could not find a suitable format in %s", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  res = IntelHDAStreamBase::CreateRingBufferLocked(std::move(format), std::move(ring_buffer));
  if (res != ZX_OK) {
    completer.Close(res);
  }
}

void IntelHDAStreamConfigBase::WatchGainState(
    StreamChannel* channel, StreamChannel::WatchGainStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->gain_completer_);
  channel->gain_completer_ = completer.ToAsync();

  OnGetGainLocked(&cur_gain_state_);

  // Reply is delayed if there is no change since the last reported gain state.
  if (channel->last_reported_gain_state_ != cur_gain_state_) {
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    if (cur_gain_state_.can_mute) {
      gain_state.set_muted(cur_gain_state_.cur_mute);
    }
    if (cur_gain_state_.can_agc) {
      gain_state.set_agc_enabled(cur_gain_state_.cur_mute);
    }
    gain_state.set_gain_db(cur_gain_state_.cur_gain);
    channel->last_reported_gain_state_ = cur_gain_state_;
    channel->gain_completer_->Reply(std::move(gain_state));
    channel->gain_completer_.reset();
  }
}

void IntelHDAStreamConfigBase::SetGain(audio_fidl::wire::GainState target_state,
                                       StreamChannel::SetGainCompleter::Sync& completer) {
  OnGetGainLocked(&cur_gain_state_);

  // Sanity check the request before passing it along
  if (target_state.has_muted() && target_state.muted() && !cur_gain_state_.can_mute) {
    LOG("Can't mute\n");
    return;
  }

  if (target_state.has_agc_enabled() && target_state.agc_enabled() && !cur_gain_state_.can_agc) {
    LOG("Can't enable AGC\n");
    return;
  }

  if (target_state.has_gain_db() && ((target_state.gain_db() < cur_gain_state_.min_gain) ||
                                     (target_state.gain_db() > cur_gain_state_.max_gain))) {
    LOG("Can't set gain outside valid range\n");
    return;
  }

  audio_stream_cmd_set_gain_req_t req = {};
  cur_gain_state_.can_mute = target_state.has_muted();
  if (cur_gain_state_.can_mute) {
    req.flags |= AUDIO_SGF_MUTE_VALID;
    if (target_state.muted()) {
      req.flags |= AUDIO_SGF_MUTE;
    }
    cur_gain_state_.cur_mute = target_state.muted();
  }
  cur_gain_state_.can_agc = target_state.has_agc_enabled();
  if (cur_gain_state_.can_agc) {
    req.flags |= AUDIO_SGF_AGC_VALID;
    if (target_state.agc_enabled()) {
      req.flags |= AUDIO_SGF_AGC;
    }
    cur_gain_state_.cur_agc = target_state.agc_enabled();
  }
  if (target_state.has_gain_db()) {
    req.flags |= AUDIO_SGF_GAIN_VALID;
    req.gain = target_state.gain_db();
    cur_gain_state_.cur_gain = req.gain;
  }

  audio_proto::SetGainResp out = {};
  OnSetGainLocked(req, &out);
  if (out.result != ZX_OK && out.result != ZX_ERR_NOT_SUPPORTED) {
    LOG("Error setting the gain state %d\n", out.result);
  }
  for (auto& channel : stream_channels_) {
    if (channel.gain_completer_) {
      channel.gain_completer_->Reply(std::move(target_state));
      channel.gain_completer_.reset();
    }
  }
}

void IntelHDAStreamConfigBase::WatchPlugState(
    StreamChannel* channel, StreamChannel::WatchPlugStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->plug_completer_);
  channel->plug_completer_ = completer.ToAsync();

  audio_proto::PlugDetectResp plug = {};

  OnPlugDetectLocked(channel, &plug);

  bool plugged = plug.flags & AUDIO_PDNF_PLUGGED;
  // Reply is delayed if there is no change since the last reported plugged state.
  if (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kNotReported ||
      (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kPlugged) != plugged) {
    fidl::Arena allocator;
    audio_fidl::wire::PlugState plug_state(allocator);
    plug_state.set_plugged(plugged).set_plug_state_time(allocator, plug.plug_state_time);
    channel->last_reported_plugged_state_ =
        plugged ? StreamChannel::Plugged::kPlugged : StreamChannel::Plugged::kUnplugged;
    channel->plug_completer_->Reply(std::move(plug_state));
    channel->plug_completer_.reset();
  }
}

void IntelHDAStreamConfigBase::NotifyPlugStateLocked(bool plugged, int64_t plug_time) {
  for (auto& channel : stream_channels_) {
    if (channel.plug_completer_) {
      fidl::Arena allocator;
      audio_fidl::wire::PlugState plug_state(allocator);
      plug_state.set_plugged(plugged).set_plug_state_time(allocator, plug_time);
      channel.plug_completer_->Reply(std::move(plug_state));
      channel.plug_completer_.reset();
    }
  }
}

void IntelHDAStreamConfigBase::GetProperties(
    StreamChannel* channel,
    fidl::WireServer<audio_fidl::StreamConfig>::GetPropertiesCompleter::Sync& completer) {
  fidl::Arena allocator;
  audio_fidl::wire::StreamProperties response(allocator);

  fidl::Array<uint8_t, audio_fidl::wire::kUniqueIdSize> unique_id = {};
  audio_stream_unique_id_t& persistent_id = GetPersistentUniqueIdLocked();
  for (size_t i = 0; i < audio_fidl::wire::kUniqueIdSize; ++i) {
    unique_id.data_[i] = persistent_id.data[i];
  }
  response.set_unique_id(allocator, unique_id);
  response.set_is_input(is_input());

  OnGetGainLocked(&cur_gain_state_);

  response.set_can_mute(cur_gain_state_.can_mute);
  response.set_can_agc(cur_gain_state_.can_agc);
  response.set_min_gain_db(cur_gain_state_.min_gain);
  response.set_max_gain_db(cur_gain_state_.max_gain);
  response.set_gain_step_db(cur_gain_state_.gain_step);

  audio_proto::GetStringResp resp_product = {};
  audio_proto::GetStringReq req = {};
  req.id = AUDIO_STREAM_STR_ID_PRODUCT;
  OnGetStringLocked(req, &resp_product);
  auto product = fidl::StringView::FromExternal(reinterpret_cast<char*>(resp_product.str),
                                                resp_product.strlen);
  response.set_product(fidl::ObjectView<fidl::StringView>::FromExternal(&product));

  req.id = AUDIO_STREAM_STR_ID_MANUFACTURER;
  audio_proto::GetStringResp resp_manufacturer = {};
  OnGetStringLocked(req, &resp_manufacturer);
  auto manufacturer = fidl::StringView::FromExternal(reinterpret_cast<char*>(resp_manufacturer.str),
                                                     resp_manufacturer.strlen);
  response.set_manufacturer(fidl::ObjectView<fidl::StringView>::FromExternal(&manufacturer));

  audio_proto::GetClockDomainResp domain_resp = {};
  OnGetClockDomainLocked(&domain_resp);
  response.set_clock_domain(domain_resp.clock_domain);

  audio_proto::PlugDetectResp plug = {};
  OnPlugDetectLocked(channel, &plug);
  if (plug.flags & AUDIO_PDNF_CAN_NOTIFY) {
    response.set_plug_detect_capabilities(
        audio_fidl::wire::PlugDetectCapabilities::kCanAsyncNotify);
  } else if (plug.flags & AUDIO_PDNF_HARDWIRED) {
    response.set_plug_detect_capabilities(audio_fidl::wire::PlugDetectCapabilities::kHardwired);
  }
  completer.Reply(std::move(response));
}

void IntelHDAStreamConfigBase::OnGetGainLocked(audio_proto::GainState* out_resp) {
  ZX_DEBUG_ASSERT(out_resp != nullptr);

  // By default we claim to have a fixed, un-mute-able gain stage.
  out_resp->cur_mute = false;
  out_resp->cur_agc = false;
  out_resp->cur_gain = 0.0;

  out_resp->can_mute = false;
  out_resp->can_agc = false;
  out_resp->min_gain = 0.0;
  out_resp->max_gain = 0.0;
  out_resp->gain_step = 0.0;
}

void IntelHDAStreamConfigBase::OnSetGainLocked(const audio_proto::SetGainReq& req,
                                               audio_proto::SetGainResp* out_resp) {
  // Nothing to do if no response is expected.
  if (out_resp == nullptr) {
    ZX_DEBUG_ASSERT(req.hdr.cmd & AUDIO_FLAG_NO_ACK);
    return;
  }

  bool illegal_mute = (req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE);
  bool illegal_agc = (req.flags & AUDIO_SGF_AGC_VALID) && (req.flags & AUDIO_SGF_AGC);
  bool illegal_gain = (req.flags & AUDIO_SGF_GAIN_VALID) && (req.gain != 0.0f);

  out_resp->cur_mute = false;
  out_resp->cur_gain = 0.0;
  out_resp->result = (illegal_mute || illegal_agc || illegal_gain) ? ZX_ERR_INVALID_ARGS : ZX_OK;
}

void IntelHDAStreamConfigBase::OnPlugDetectLocked(StreamChannel* response_channel,
                                                  audio_proto::PlugDetectResp* out_resp) {
  // Nothing to do if no response is expected.
  if (out_resp == nullptr) {
    return;
  }

  ZX_DEBUG_ASSERT(parent_codec() != nullptr);
  out_resp->plug_state_time = parent_codec()->create_time();
  out_resp->flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_PLUGGED);
}

void IntelHDAStreamConfigBase::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                                 audio_proto::GetStringResp* out_resp) {
  ZX_DEBUG_ASSERT(out_resp);

  switch (req.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
    case AUDIO_STREAM_STR_ID_PRODUCT: {
      int res =
          snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "<unknown>");
      ZX_DEBUG_ASSERT(res >= 0);  // there should be no way for snprintf to fail here.
      out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
      out_resp->result = ZX_OK;
      break;
    }

    default:
      out_resp->strlen = 0;
      out_resp->result = ZX_ERR_NOT_FOUND;
      break;
  }
}

void IntelHDAStreamConfigBase::OnGetClockDomainLocked(audio_proto::GetClockDomainResp* out_resp) {
  ZX_DEBUG_ASSERT(out_resp != nullptr);

  // By default we claim to be in the MONOTONIC clock domain.
  // TODO(mpuryear): if the audio clock might possibly ever be in a different domain than the local
  // system clock (either because it is trimmable [unlikely] or uses a different oscillator [even
  // less likely]), handle that case here.
  out_resp->clock_domain = 0;
}

void IntelHDAStreamConfigBase::ProcessClientDeactivateLocked(StreamChannel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // Let our subclass know that this channel is going away.
  OnChannelDeactivateLocked(*channel);

  // Is this the privileged stream channel?
  if (stream_channel_.get() == channel) {
    stream_channel_.reset();
  }

  stream_channels_.erase(*channel);
}

void IntelHDAStreamConfigBase::OnChannelDeactivateLocked(const StreamChannel& channel) {}

void IntelHDAStreamConfigBase::OnDeactivate() { loop_.Shutdown(); }

void IntelHDAStreamConfigBase::RemoveDeviceLocked() { device_async_remove(stream_device_); }

zx_status_t IntelHDAStreamConfigBase::ProcessSetStreamFmtLocked(
    const ihda_proto::SetStreamFmtResp& codec_resp) {
  zx_status_t res = ZX_OK;

  // Are we shutting down?
  if (!is_active())
    return ZX_ERR_BAD_STATE;

  // If we don't have a set format operation in flight, or the stream channel
  // has been closed, this set format operation has been canceled.  Do not
  // return an error up the stack; we don't want to close the connection to
  // our codec device.
  if ((!IsFormatChangeInProgress()) || (stream_channel_ == nullptr)) {
    goto finished;
  }

  // Let the implementation send the commands required to finish changing the
  // stream format.
  res = FinishChangeStreamFormatLocked(encoded_fmt());
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt(), res);
    goto finished;
  }

finished:
  // Something went fatally wrong when trying to send the result back to the
  // caller.  Close the stream channel.
  if (stream_channel_ != nullptr) {
    OnChannelDeactivateLocked(*stream_channel_);
    stream_channel_ = nullptr;
  }

  // Set format operation is finished. There is no reply sent in CreateRingBuffer.
  SetFormatChangeInProgress(false);

  return ZX_OK;
}

zx_status_t IntelHDAStreamConfigBase::PublishDeviceLocked() {
  if (!is_active())
    return ZX_ERR_BAD_STATE;

  // Initialize our device and fill out the protocol hooks
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = dev_name();
  args.ctx = this;
  args.ops = &STREAM_DEVICE_THUNKS;
  args.proto_id = (is_input() ? ZX_PROTOCOL_AUDIO_INPUT : ZX_PROTOCOL_AUDIO_OUTPUT);

  // Publish the device.
  zx_status_t res = device_add(parent_codec()->codec_device(), &args, &stream_device_);
  if (res != ZX_OK) {
    LOG("Failed to add stream device for \"%s\" (res %d)\n", dev_name(), res);
    return res;
  }

  return IntelHDAStreamBase::RecordPublishedDeviceLocked();
}

}  // namespace audio::intel_hda::codecs
