// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/server.h>
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
#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "debug-logging.h"

namespace {
namespace audio_fidl = fuchsia_hardware_audio;
}  // namespace

namespace audio {
namespace intel_hda {
namespace codecs {

zx_protocol_device_t IntelHDAStreamBase::STREAM_DEVICE_THUNKS = []() {
  zx_protocol_device_t sdt = {};
  sdt.version = DEVICE_OPS_VERSION;
  sdt.message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    IntelHDAStreamBase* thiz = static_cast<IntelHDAStreamBase*>(ctx);
    DdkTransaction transaction(txn);
    fidl::WireDispatch<fuchsia_hardware_audio::Device>(
        thiz, fidl::IncomingMessage::FromEncodedCMessage(msg), &transaction);
    return transaction.Status();
  };
  return sdt;
}();

IntelHDAStreamBase::IntelHDAStreamBase(uint32_t id, bool is_input)
    : id_(id), is_input_(is_input), loop_(&kAsyncLoopConfigNeverAttachToThread) {
  plug_time_ = zx::clock::get_monotonic().get();
  snprintf(dev_name_, sizeof(dev_name_), "%s-stream-%03u", is_input_ ? "input" : "output", id_);
  loop_.StartThread("intel-hda-stream-loop");
}

IntelHDAStreamBase::~IntelHDAStreamBase() {}

void IntelHDAStreamBase::PrintDebugPrefix() const { printf("[%s] ", dev_name_); }

void IntelHDAStreamBase::SetPersistentUniqueId(const audio_stream_unique_id_t& id) {
  fbl::AutoLock obj_lock(&obj_lock_);
  SetPersistentUniqueIdLocked(id);
}

void IntelHDAStreamBase::SetPersistentUniqueIdLocked(const audio_stream_unique_id_t& id) {
  persistent_unique_id_ = id;
}

zx_status_t IntelHDAStreamBase::Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                                         const fbl::RefPtr<Channel>& codec_channel) {
  ZX_DEBUG_ASSERT(codec_channel != nullptr);

  fbl::AutoLock obj_lock(&obj_lock_);
  if (is_active() || (codec_channel_ != nullptr))
    return ZX_ERR_BAD_STATE;

  ZX_DEBUG_ASSERT(parent_codec_ == nullptr);

  // Remember our parent codec and our codec channel.  If something goes wrong
  // during activation, make sure we let go of these references.
  //
  // Note; the cleanup lambda needs to have thread analysis turned off because
  // the compiler is not quite smart enough to figure out that the obj_lock
  // AutoLock will destruct (and release the lock) after the AutoCall runs,
  // and that the AutoCall will never leave this scope.
  auto cleanup = fit::defer([this]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    parent_codec_.reset();
    codec_channel_.reset();
  });
  parent_codec_ = std::move(parent_codec);
  codec_channel_ = codec_channel;

  // Allow our implementation to send its initial stream setup commands to the
  // codec.
  zx_status_t res = OnActivateLocked();
  if (res != ZX_OK)
    return res;

  // Request a DMA context
  ihda_proto::RequestStreamReq req;

  req.hdr.transaction_id = id();
  req.hdr.cmd = IHDA_CODEC_REQUEST_STREAM;
  req.input = is_input();

  res = codec_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK)
    return res;

  cleanup.cancel();
  return ZX_OK;
}

void IntelHDAStreamBase::Deactivate() {
  {
    fbl::AutoLock obj_lock(&obj_lock_);
    DEBUG_LOG("Deactivating stream\n");

    // Let go of any unsolicited stream tags we may be holding.
    if (unsol_tag_count_) {
      ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
      parent_codec_->ReleaseAllUnsolTags(*this);
      unsol_tag_count_ = 0;
    }

    // Clear out our parent_codec_ pointer.  This will mark us as being
    // inactive and prevent any new connections from being made.
    parent_codec_.reset();

    // We should already have been removed from our codec's active stream list
    // at this point.
    ZX_DEBUG_ASSERT(!this->InContainer());
  }

  loop_.Shutdown();

  {
    fbl::AutoLock obj_lock(&obj_lock_);
    ZX_DEBUG_ASSERT(stream_channel_ == nullptr);

    // Allow our implementation to send the commands needed to tear down the
    // widgets which make up this stream.
    OnDeactivateLocked();

    // If we have been given a DMA stream by the IHDA controller, attempt to
    // return it now.
    if ((dma_stream_id_ != IHDA_INVALID_STREAM_ID) && (codec_channel_ != nullptr)) {
      ihda_proto::ReleaseStreamReq req;

      req.hdr.transaction_id = id();
      req.hdr.cmd = IHDA_CODEC_RELEASE_STREAM_NOACK, req.stream_id = dma_stream_id_;

      codec_channel_->Write(&req, sizeof(req));

      dma_stream_id_ = IHDA_INVALID_STREAM_ID;
      dma_stream_tag_ = IHDA_INVALID_STREAM_TAG;
    }

    // Let go of our reference to the codec device channel.
    codec_channel_ = nullptr;

    // If we had published a device node, remove it now.
    if (parent_device_ != nullptr) {
      device_async_remove(stream_device_);
      parent_device_ = nullptr;
    }
  }

  DEBUG_LOG("Deactivate complete\n");
}

zx_status_t IntelHDAStreamBase::PublishDeviceLocked() {
  if (!is_active() || (parent_device_ != nullptr))
    return ZX_ERR_BAD_STATE;
  ZX_DEBUG_ASSERT(parent_codec_ != nullptr);

  // Initialize our device and fill out the protocol hooks
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = dev_name_;
  args.ctx = this;
  args.ops = &STREAM_DEVICE_THUNKS;
  args.proto_id = (is_input() ? ZX_PROTOCOL_AUDIO_INPUT : ZX_PROTOCOL_AUDIO_OUTPUT);

  // Publish the device.
  zx_status_t res = device_add(parent_codec_->codec_device(), &args, &stream_device_);
  if (res != ZX_OK) {
    LOG("Failed to add stream device for \"%s\" (res %d)\n", dev_name_, res);
    return res;
  }

  // Record our parent.
  parent_device_ = parent_codec_->codec_device();

  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::ProcessResponse(const CodecResponse& resp) {
  fbl::AutoLock obj_lock(&obj_lock_);

  if (!is_active()) {
    DEBUG_LOG("Ignoring codec response (0x%08x, 0x%08x) for inactive stream id %u\n", resp.data,
              resp.data_ex, id());
    return ZX_OK;
  }

  return resp.unsolicited() ? OnUnsolicitedResponseLocked(resp) : OnSolicitedResponseLocked(resp);
}

zx_status_t IntelHDAStreamBase::ProcessRequestStream(const ihda_proto::RequestStreamResp& resp) {
  fbl::AutoLock obj_lock(&obj_lock_);
  zx_status_t res;

  if (!is_active())
    return ZX_ERR_BAD_STATE;

  res = SetDMAStreamLocked(resp.stream_id, resp.stream_tag);
  if (res != ZX_OK) {
    // TODO(johngro) : If we failed to set the DMA info because this stream
    // is in the process of shutting down, we really should return the
    // stream to the controller.
    //
    // Right now, we are going to return an error which will cause the lower
    // level infrastructure to close the codec device channel.  This will
    // prevent a leak (the core controller driver will re-claim the stream),
    // but it will also ruin all of the other streams in this codec are
    // going to end up being destroyed.  For simple codec driver who never
    // change stream topology, this is probably fine, but for more
    // complicated ones it probably is not.
    return res;
  }

  return OnDMAAssignedLocked();
}

zx_status_t IntelHDAStreamBase::ProcessSetStreamFmt(
    const ihda_proto::SetStreamFmtResp& codec_resp) {
  fbl::AutoLock obj_lock(&obj_lock_);
  zx_status_t res = ZX_OK;

  // Are we shutting down?
  if (!is_active())
    return ZX_ERR_BAD_STATE;

  // If we don't have a set format operation in flight, or the stream channel
  // has been closed, this set format operation has been canceled.  Do not
  // return an error up the stack; we don't want to close the connection to
  // our codec device.
  if ((!IsFormatChangeInProgress()) || (stream_channel_ == nullptr))
    goto finished;

  // Let the implementation send the commands required to finish changing the
  // stream format.
  res = FinishChangeStreamFormatLocked(encoded_fmt_);
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt_, res);
    goto finished;
  }

finished:
  // Something went fatally wrong when trying to send the result back to the
  // caller.  Close the stream channel.
  if ((res != ZX_OK) && (stream_channel_ != nullptr)) {
    OnChannelDeactivateLocked(*stream_channel_);
    stream_channel_ = nullptr;
  }

  // Set format operation is finished. There is no reply sent in CreateRingBuffer.
  SetFormatChangeInProgress(false);

  return ZX_OK;
}

// TODO(johngro) : Refactor this; this sample_format of parameters is 95% the same
// between both the codec and stream base classes.
zx_status_t IntelHDAStreamBase::SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack) {
  if (codec_channel_ == nullptr)
    return ZX_ERR_BAD_STATE;

  ihda_codec_send_corb_cmd_req_t cmd;

  cmd.hdr.cmd = (do_ack == Ack::NO) ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
  cmd.hdr.transaction_id = id();
  cmd.nid = nid;
  cmd.verb = verb.val;

  return codec_channel_->Write(&cmd, sizeof(cmd));
}

zx_status_t IntelHDAStreamBase::SetDMAStreamLocked(uint16_t id, uint8_t tag) {
  if ((id == IHDA_INVALID_STREAM_ID) || (tag == IHDA_INVALID_STREAM_TAG))
    return ZX_ERR_INVALID_ARGS;

  ZX_DEBUG_ASSERT((dma_stream_id_ == IHDA_INVALID_STREAM_ID) ==
                  (dma_stream_tag_ == IHDA_INVALID_STREAM_TAG));

  if (dma_stream_id_ != IHDA_INVALID_STREAM_ID)
    return ZX_ERR_BAD_STATE;

  dma_stream_id_ = id;
  dma_stream_tag_ = tag;

  return ZX_OK;
}

void IntelHDAStreamBase::GetChannel(GetChannelRequestView request,
                                    GetChannelCompleter::Sync& completer) {
  fbl::AutoLock obj_lock(&obj_lock_);

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
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfig>();
  if (!endpoints.is_ok()) {
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  auto [stream_channel_remote, stream_channel_local] = *std::move(endpoints);

  fbl::RefPtr<StreamChannel> stream_channel = StreamChannel::Create(this);
  if (stream_channel == nullptr) {
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  stream_channels_.push_back(stream_channel);

  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::StreamConfig>> on_unbound =
      [this, stream_channel](fidl::WireServer<audio_fidl::StreamConfig>*, fidl::UnbindInfo,
                             fidl::ServerEnd<fuchsia_hardware_audio::StreamConfig>) {
        fbl::AutoLock channel_lock(&this->obj_lock_);
        this->ProcessClientDeactivateLocked(stream_channel.get());
      };

  fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfig>>(
      loop_.dispatcher(), std::move(stream_channel_local), stream_channel.get(),
      std::move(on_unbound));

  if (privileged) {
    stream_channel_ = stream_channel;
  }

  completer.Reply(std::move(stream_channel_remote));
}

void IntelHDAStreamBase::GetSupportedFormats(
    StreamChannel::GetSupportedFormatsCompleter::Sync& completer) {
  fbl::AutoLock channel_lock(&obj_lock_);

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

void IntelHDAStreamBase::CreateRingBuffer(
    StreamChannel* channel, audio_fidl::wire::Format format,
    ::fidl::ServerEnd<audio_fidl::RingBuffer> ring_buffer,
    StreamChannel::CreateRingBufferCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(channel != nullptr);
  ihda_proto::SetStreamFmtReq req;
  uint16_t encoded_fmt;
  zx_status_t res;

  fbl::AutoLock channel_lock(&obj_lock_);
  // Only the privileged stream channel is allowed to change the format.
  if (channel != stream_channel_.get()) {
    LOG("Unprivileged channel cannot set the format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // If we don't have a DMA stream assigned to us, or there is already a set
  // format operation in flight, we cannot proceed.
  if ((dma_stream_id_ == IHDA_INVALID_STREAM_ID) || IsFormatChangeInProgress()) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  auto format_pcm = format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      format_pcm.valid_bits_per_sample, 8 * format_pcm.bytes_per_sample);

  if (sample_format == 0) {
    LOG("Unsupported format: Invalid bits per sample (%u/%u)\n", format_pcm.valid_bits_per_sample,
        8 * format_pcm.bytes_per_sample);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (format_pcm.sample_format == audio_fidl::wire::SampleFormat::kPcmFloat) {
    sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    if (format_pcm.valid_bits_per_sample != 32 || format_pcm.bytes_per_sample != 4) {
      LOG("Unsupported format: Not 32 per sample/channel for float\n");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  if (format_pcm.sample_format == audio_fidl::wire::SampleFormat::kPcmUnsigned) {
    sample_format |= AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  }

  bool found_one = false;
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

  audio_proto::StreamSetFmtReq fmt = {};
  fmt.sample_format = sample_format;
  fmt.channels = format_pcm.number_of_channels;
  fmt.frames_per_second = format_pcm.frame_rate;

  // The upper level stream told us that they support this format, we had
  // better be able to encode it into an IHDA format specifier.
  res = EncodeStreamFormat(fmt, &encoded_fmt);
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to encode stream format %u:%hu:%s (res %d)\n", fmt.frames_per_second,
              fmt.channels, audio_proto::SampleFormatToString(fmt.sample_format), res);
    completer.Close(res);
    return;
  }

  // Let our implementation start the process of a format change.  This gives
  // it a chance to check the format for compatibility, and send commands to
  // quiesce the converters and amplifiers if it approves of the format.
  res = BeginChangeStreamFormatLocked(fmt);
  if (res != ZX_OK) {
    DEBUG_LOG("Stream impl rejected stream format %u:%hu:%s (res %d)\n", fmt.frames_per_second,
              fmt.channels, audio_proto::SampleFormatToString(fmt.sample_format), res);
    completer.Close(res);
    return;
  }

  // Set the format of DMA stream.  This will stop any stream in progress and
  // close any connection to its clients.  At this point, all of our checks
  // are done and we expect success.  If anything goes wrong, consider it to
  // be a fatal internal error and close the connection to our client by
  // returning an error.
  ZX_DEBUG_ASSERT(codec_channel_ != nullptr);
  req.hdr.cmd = IHDA_CODEC_SET_STREAM_FORMAT;
  req.hdr.transaction_id = id();
  req.stream_id = dma_stream_id_;
  req.format = encoded_fmt;
  res = codec_channel_->Write(&req, sizeof(req), ring_buffer.TakeChannel());
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to write set stream format %u:%hu:%s to codec channel (res %d)\n",
              fmt.frames_per_second, fmt.channels,
              audio_proto::SampleFormatToString(fmt.sample_format), res);
    completer.Close(res);
    return;
  }

  // Success!  Record that the format change is in progress.
  SetFormatChangeInProgress(true);
  encoded_fmt_ = encoded_fmt;
}

void IntelHDAStreamBase::WatchGainState(StreamChannel* channel,
                                        StreamChannel::WatchGainStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->gain_completer_);
  channel->gain_completer_ = completer.ToAsync();

  fbl::AutoLock obj_lock(&obj_lock_);
  OnGetGainLocked(&cur_gain_state_);

  // Reply is delayed if there is no change since the last reported gain state.
  if (channel->last_reported_gain_state_ != cur_gain_state_) {
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    if (cur_gain_state_.can_mute) {
      gain_state.set_muted(allocator, cur_gain_state_.cur_mute);
    }
    if (cur_gain_state_.can_agc) {
      gain_state.set_agc_enabled(allocator, cur_gain_state_.cur_mute);
    }
    gain_state.set_gain_db(allocator, cur_gain_state_.cur_gain);
    channel->last_reported_gain_state_ = cur_gain_state_;
    channel->gain_completer_->Reply(std::move(gain_state));
    channel->gain_completer_.reset();
  }
}

void IntelHDAStreamBase::SetGain(audio_fidl::wire::GainState target_state,
                                 StreamChannel::SetGainCompleter::Sync& completer) {
  fbl::AutoLock obj_lock(&obj_lock_);
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

void IntelHDAStreamBase::WatchPlugState(StreamChannel* channel,
                                        StreamChannel::WatchPlugStateCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(!channel->plug_completer_);
  channel->plug_completer_ = completer.ToAsync();

  audio_proto::PlugDetectResp plug = {};

  fbl::AutoLock lock(obj_lock());
  OnPlugDetectLocked(channel, &plug);

  bool plugged = plug.flags & AUDIO_PDNF_PLUGGED;
  // Reply is delayed if there is no change since the last reported plugged state.
  if (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kNotReported ||
      (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kPlugged) != plugged) {
    fidl::Arena allocator;
    audio_fidl::wire::PlugState plug_state(allocator);
    plug_state.set_plugged(allocator, plugged).set_plug_state_time(allocator, plug.plug_state_time);
    channel->last_reported_plugged_state_ =
        plugged ? StreamChannel::Plugged::kPlugged : StreamChannel::Plugged::kUnplugged;
    channel->plug_completer_->Reply(std::move(plug_state));
    channel->plug_completer_.reset();
  }
}

void IntelHDAStreamBase::NotifyPlugStateLocked(bool plugged, int64_t plug_time) {
  for (auto& channel : stream_channels_) {
    if (channel.plug_completer_) {
      fidl::Arena allocator;
      audio_fidl::wire::PlugState plug_state(allocator);
      plug_state.set_plugged(allocator, plugged).set_plug_state_time(allocator, plug_time);
      channel.plug_completer_->Reply(std::move(plug_state));
      channel.plug_completer_.reset();
    }
  }
}

void IntelHDAStreamBase::GetProperties(
    StreamChannel* channel,
    fidl::WireServer<audio_fidl::StreamConfig>::GetPropertiesCompleter::Sync& completer) {
  fbl::AutoLock obj_lock(&obj_lock_);
  fidl::Arena allocator;
  audio_fidl::wire::StreamProperties response(allocator);

  fidl::Array<uint8_t, audio_fidl::wire::kUniqueIdSize> unique_id = {};
  for (size_t i = 0; i < audio_fidl::wire::kUniqueIdSize; ++i) {
    unique_id.data_[i] = persistent_unique_id_.data[i];
  }
  response.set_unique_id(allocator, unique_id);
  response.set_is_input(allocator, is_input());

  OnGetGainLocked(&cur_gain_state_);

  response.set_can_mute(allocator, cur_gain_state_.can_mute);
  response.set_can_agc(allocator, cur_gain_state_.can_agc);
  response.set_min_gain_db(allocator, cur_gain_state_.min_gain);
  response.set_max_gain_db(allocator, cur_gain_state_.max_gain);
  response.set_gain_step_db(allocator, cur_gain_state_.gain_step);

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
  response.set_clock_domain(allocator, domain_resp.clock_domain);

  audio_proto::PlugDetectResp plug = {};
  OnPlugDetectLocked(channel, &plug);
  if (plug.flags & AUDIO_PDNF_CAN_NOTIFY) {
    response.set_plug_detect_capabilities(
        allocator, audio_fidl::wire::PlugDetectCapabilities::kCanAsyncNotify);
  } else if (plug.flags & AUDIO_PDNF_HARDWIRED) {
    response.set_plug_detect_capabilities(allocator,
                                          audio_fidl::wire::PlugDetectCapabilities::kHardwired);
  }
  completer.Reply(std::move(response));
}

void IntelHDAStreamBase::ProcessClientDeactivateLocked(StreamChannel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // Let our subclass know that this channel is going away.
  OnChannelDeactivateLocked(*channel);

  // Is this the privileged stream channel?
  if (stream_channel_.get() == channel) {
    stream_channel_.reset();
  }

  stream_channels_.erase(*channel);
}

zx_status_t IntelHDAStreamBase::AllocateUnsolTagLocked(uint8_t* out_tag) {
  if (!parent_codec_)
    return ZX_ERR_BAD_STATE;

  zx_status_t res = parent_codec_->AllocateUnsolTag(*this, out_tag);
  if (res == ZX_OK)
    unsol_tag_count_++;

  return res;
}

void IntelHDAStreamBase::ReleaseUnsolTagLocked(uint8_t tag) {
  ZX_DEBUG_ASSERT(unsol_tag_count_ > 0);
  ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
  parent_codec_->ReleaseUnsolTag(*this, tag);
  unsol_tag_count_--;
}

// TODO(johngro) : Move this out to a utils library?
#define MAKE_RATE(_rate, _base, _mult, _div) \
  { .rate = _rate, .encoded = (_base << 14) | ((_mult - 1) << 11) | ((_div - 1) << 8) }
zx_status_t IntelHDAStreamBase::EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                                   uint16_t* encoded_fmt_out) {
  ZX_DEBUG_ASSERT(encoded_fmt_out != nullptr);

  // See section 3.7.1
  // Start with the channel count.  Intel HDA DMA streams support between 1
  // and 16 channels.
  uint32_t channels = fmt.channels - 1;
  if ((fmt.channels < 1) || (fmt.channels > 16))
    return ZX_ERR_NOT_SUPPORTED;

  // Next determine the bit sample_format format
  uint32_t bits;
  switch (fmt.sample_format) {
    case AUDIO_SAMPLE_FORMAT_8BIT:
      bits = 0;
      break;
    case AUDIO_SAMPLE_FORMAT_16BIT:
      bits = 1;
      break;
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
      bits = 2;
      break;
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
      bits = 3;
      break;
    case AUDIO_SAMPLE_FORMAT_32BIT:
    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:
      bits = 4;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  // Finally, determine the base frame rate, as well as the multiplier and
  // divisor.
  static const struct {
    uint32_t rate;
    uint32_t encoded;
  } RATE_ENCODINGS[] = {
      // 48 KHz family
      MAKE_RATE(6000, 0, 1, 8),
      MAKE_RATE(8000, 0, 1, 6),
      MAKE_RATE(9600, 0, 1, 5),
      MAKE_RATE(16000, 0, 1, 3),
      MAKE_RATE(24000, 0, 1, 2),
      MAKE_RATE(32000, 0, 2, 3),
      MAKE_RATE(48000, 0, 1, 1),
      MAKE_RATE(96000, 0, 2, 1),
      MAKE_RATE(144000, 0, 3, 1),
      MAKE_RATE(192000, 0, 4, 1),
      // 44.1 KHz family
      MAKE_RATE(11025, 1, 1, 4),
      MAKE_RATE(22050, 1, 1, 2),
      MAKE_RATE(44100, 1, 1, 1),
      MAKE_RATE(88200, 1, 2, 1),
      MAKE_RATE(176400, 1, 4, 1),
  };

  for (const auto& r : RATE_ENCODINGS) {
    if (r.rate == fmt.frames_per_second) {
      *encoded_fmt_out = static_cast<uint16_t>(r.encoded | channels | (bits << 4));
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}
#undef MAKE_RATE

/////////////////////////////////////////////////////////////////////
//
// Default handlers
//
/////////////////////////////////////////////////////////////////////
zx_status_t IntelHDAStreamBase::OnActivateLocked() { return ZX_OK; }

void IntelHDAStreamBase::OnDeactivateLocked() {}
void IntelHDAStreamBase::OnChannelDeactivateLocked(const StreamChannel& channel) {}

zx_status_t IntelHDAStreamBase::OnDMAAssignedLocked() { return PublishDeviceLocked(); }

zx_status_t IntelHDAStreamBase::OnSolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::BeginChangeStreamFormatLocked(
    const audio_proto::StreamSetFmtReq& fmt) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelHDAStreamBase::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  return ZX_ERR_INTERNAL;
}

void IntelHDAStreamBase::OnGetGainLocked(audio_proto::GainState* out_resp) {
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

void IntelHDAStreamBase::OnSetGainLocked(const audio_proto::SetGainReq& req,
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

void IntelHDAStreamBase::OnPlugDetectLocked(StreamChannel* response_channel,
                                            audio_proto::PlugDetectResp* out_resp) {
  // Nothing to do if no response is expected.
  if (out_resp == nullptr) {
    return;
  }

  ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
  out_resp->flags = static_cast<audio_pd_notify_flags_t>(AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_PLUGGED);
  out_resp->plug_state_time = parent_codec_->create_time();
}

void IntelHDAStreamBase::OnGetStringLocked(const audio_proto::GetStringReq& req,
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

void IntelHDAStreamBase::OnGetClockDomainLocked(audio_proto::GetClockDomainResp* out_resp) {
  ZX_DEBUG_ASSERT(out_resp != nullptr);

  // By default we claim to be in the MONOTONIC clock domain.
  // TODO(mpuryear): if the audio clock might possibly ever be in a different domain than the local
  // system clock (either because it is trimmable [unlikely] or uses a different oscillator [even
  // less likely]), handle that case here.
  out_resp->clock_domain = 0;
}

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
