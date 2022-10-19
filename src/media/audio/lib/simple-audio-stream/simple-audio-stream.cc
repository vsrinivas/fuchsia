// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/ddk/debug.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/device/audio.h>

#include <limits>
#include <utility>

#include <audio-proto-utils/format-utils.h>
namespace audio {

SimpleAudioStream::SimpleAudioStream(zx_device_t* parent, bool is_input)
    : SimpleAudioStreamBase(parent),
      SimpleAudioStreamProtocol(is_input),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  simple_audio_ = inspect_.GetRoot().CreateChild("simple_audio_stream");
  state_ = simple_audio_.CreateString("state", "created");
  start_time_ = simple_audio_.CreateInt("start_time", 0);
  position_request_time_ = simple_audio_.CreateInt("position_request_time", 0);
  position_reply_time_ = simple_audio_.CreateInt("position_reply_time", 0);
  ring_buffer_size_ = simple_audio_.CreateUint("ring_buffer_size", 0);
  frames_requested_ = simple_audio_.CreateUint("frames_requested", 0);

  number_of_channels_ = simple_audio_.CreateUint("number_of_channels", 0);
  channels_to_use_bitmask_ = simple_audio_.CreateUint("channels_to_use_bitmask", 0);
  frame_rate_ = simple_audio_.CreateUint("frame_rate", 0);
  bits_per_slot_ = simple_audio_.CreateUint("bits_per_slot", 0);
  bits_per_sample_ = simple_audio_.CreateUint("bits_per_sample", 0);
  sample_format_ = simple_audio_.CreateString("sample_format", "not_set");
}

void SimpleAudioStream::Shutdown() {
  bool already_shutting_down;
  {
    fbl::AutoLock channel_lock(&channel_lock_);
    already_shutting_down = shutting_down_;
    shutting_down_ = true;
  }
  if (!already_shutting_down) {
    loop_.Shutdown();

    // We have shutdown our loop, it is now safe to assert we are holding the domain token.
    ScopedToken t(domain_token());

    {
      // Now we explicitly destroy the channels.
      fbl::AutoLock channel_lock(&channel_lock_);
      DeactivateRingBufferChannel(rb_channel_.get());

      stream_channels_.clear();
      stream_channel_ = nullptr;
    }

    ShutdownHook();
  }
}

zx_status_t SimpleAudioStream::CreateInternal() {
  zx_status_t res;

  {
    // We have not created the domain yet, it should be safe to pretend that
    // we have the token (since we know that no dispatches are going to be
    // invoked from the non-existent domain at this point)
    ScopedToken t(domain_token());
    res = Init();
    if (res != ZX_OK) {
      zxlogf(ERROR, "Failed during initialization (err %d)", res);
      return res;
    }
    // If no subclass has set this, we need to do so here.
    if (plug_time_ == 0) {
      plug_time_ = zx::clock::get_monotonic().get();
    }
  }

  // TODO(fxbug.dev/37372): Add profile configuration.
  // This single threaded dispatcher serializes the FIDL server implementation by this class.
  loop_.StartThread("simple-audio-stream-loop");

  res = PublishInternal();
  if (res != ZX_OK) {
    zxlogf(ERROR, "Failed during publishing (err %d)", res);
    return res;
  }

  return ZX_OK;
}

zx_status_t SimpleAudioStream::PublishInternal() {
  device_name_[sizeof(device_name_) - 1] = 0;
  if (!strlen(device_name_)) {
    zxlogf(ERROR, "Zero-length device name");
    return ZX_ERR_BAD_STATE;
  }

  // If we succeed in adding our device, add an explicit reference to
  // ourselves to represent the reference now being held by the DDK.  We will
  // get this reference back when the DDK (eventually) calls release.
  zx_status_t res =
      DdkAdd(ddk::DeviceAddArgs(device_name_).set_inspect_vmo(inspect_.DuplicateVmo()));
  if (res == ZX_OK) {
    AddRef();
  }

  return res;
}

// Called by a child subclass during Init, to establish the properties of this plug.
// Caller must include only flags defined for audio_stream_cmd_plug_detect_resp_t.
void SimpleAudioStream::SetInitialPlugState(audio_pd_notify_flags_t initial_state) {
  audio_pd_notify_flags_t known_flags =
      AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_CAN_NOTIFY | AUDIO_PDNF_PLUGGED;
  ZX_DEBUG_ASSERT((initial_state & known_flags) == initial_state);

  pd_flags_ = initial_state;
  plug_time_ = zx::clock::get_monotonic().get();
}

// Called by a child subclass when a dynamic plug state change occurs.
// Special behavior if this isn't actually a change, or if we should not be able to unplug.
zx_status_t SimpleAudioStream::SetPlugState(bool plugged) {
  if (plugged == ((pd_flags_ & AUDIO_PDNF_PLUGGED) != 0)) {
    return ZX_OK;
  }

  ZX_DEBUG_ASSERT(((pd_flags_ & AUDIO_PDNF_HARDWIRED) == 0) || plugged);

  if (plugged) {
    pd_flags_ |= AUDIO_PDNF_PLUGGED;
  } else {
    pd_flags_ &= ~AUDIO_PDNF_PLUGGED;
  }
  plug_time_ = zx::clock::get_monotonic().get();

  if (pd_flags_ & AUDIO_PDNF_CAN_NOTIFY) {
    return NotifyPlugDetect();
  }

  return ZX_OK;
}

// Asynchronously notify of plug state changes.
zx_status_t SimpleAudioStream::NotifyPlugDetect() {
  fbl::AutoLock channel_lock(&channel_lock_);
  for (auto& channel : stream_channels_) {
    if (channel.plug_completer_) {
      fidl::Arena allocator;
      audio_fidl::wire::PlugState plug_state(allocator);
      plug_state.set_plugged(pd_flags_ & AUDIO_PDNF_PLUGGED)
          .set_plug_state_time(allocator, plug_time_);

      channel.plug_completer_->Reply(std::move(plug_state));
      channel.plug_completer_.reset();
    }
  }
  return ZX_OK;
}

zx_status_t SimpleAudioStream::NotifyPosition(const audio_proto::RingBufPositionNotify& notif) {
  fbl::AutoLock channel_lock(&channel_lock_);
  if (!expected_notifications_per_ring_.load() || (rb_channel_ == nullptr)) {
    return ZX_ERR_BAD_STATE;
  }
  audio_fidl::wire::RingBufferPositionInfo position_info = {};
  position_info.position = notif.ring_buffer_pos;
  position_info.timestamp = notif.monotonic_time;

  fbl::AutoLock position_lock(&position_lock_);
  if (position_completer_) {
    position_completer_->Reply(position_info);
    position_completer_.reset();
    position_reply_time_.Set(zx::clock::get_monotonic().get());
  }
  return ZX_OK;
}

void SimpleAudioStream::DdkUnbind(ddk::UnbindTxn txn) {
  Shutdown();

  // TODO(johngro): We need to signal our SimpleAudioStream owner to let them
  // know that we have been unbound and are in the process of shutting down.

  // Unpublish our device node.
  txn.Reply();
}

void SimpleAudioStream::DdkRelease() {
  // Recover our ref from the DDK, then let it fall out of scope.
  auto thiz = fbl::ImportFromRawPtr(this);
}

void SimpleAudioStream::DdkSuspend(ddk::SuspendTxn txn) {
  // TODO(fxbug.dev/42613): Implement proper power management based on the requested state.
  Shutdown();
  txn.Reply(ZX_OK, txn.requested_state());
}

void SimpleAudioStream::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  fbl::AutoLock channel_lock(&channel_lock_);
  if (shutting_down_) {
    zxlogf(ERROR, "Can't retrieve the stream channel -- we are closing");
    return completer.Close(ZX_ERR_BAD_STATE);
  }

  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have an stream_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  bool privileged = (stream_channel_ == nullptr);

  auto stream_channel = StreamChannel::Create<StreamChannel>(this);
  // We keep alive all channels in stream_channels_ (protected by channel_lock_).
  stream_channels_.push_back(stream_channel);
  fidl::OnUnboundFn<fidl::WireServer<audio_fidl::StreamConfig>> on_unbound =
      [this, stream_channel](fidl::WireServer<audio_fidl::StreamConfig>*, fidl::UnbindInfo info,
                             fidl::ServerEnd<fuchsia_hardware_audio::StreamConfig>) {
        // Do not log canceled cases which happens too often in particular in test cases.
        if (info.status() != ZX_ERR_CANCELED) {
          zxlogf(INFO, "StreamConf channel closing: %s", info.FormatDescription().c_str());
        }
        ScopedToken t(domain_token());
        fbl::AutoLock channel_lock(&channel_lock_);
        this->DeactivateStreamChannel(stream_channel.get());
      };

  fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfig>>(
      dispatcher(), std::move(request->protocol), stream_channel.get(), std::move(on_unbound));

  if (privileged) {
    ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
    stream_channel_ = stream_channel;
  }
}

void SimpleAudioStream::DeactivateStreamChannel(StreamChannel* channel) {
  if (stream_channel_.get() == channel) {
    stream_channel_ = nullptr;
  }

  // Any pending LLCPP completer must be either replied to or closed before we destroy it.
  if (channel->plug_completer_.has_value()) {
    zxlogf(ERROR, "Plug completer is still open when deactivating stream channel");
    channel->plug_completer_->Close(ZX_ERR_INTERNAL);
  }
  channel->plug_completer_.reset();

  // Any pending LLCPP completer must be either replied to or closed before we destroy it.
  if (channel->gain_completer_.has_value()) {
    zxlogf(ERROR, "Gain completer is still open when deactivating stream channel");
    channel->gain_completer_->Close(ZX_ERR_INTERNAL);
  }
  channel->gain_completer_.reset();

  stream_channels_.erase(*channel);  // Must be last since we may destruct *channel.
}

void SimpleAudioStream::DeactivateRingBufferChannel(const Channel* channel) {
  if (rb_channel_.get() == channel) {
    if (rb_started_) {
      Stop();
      rb_started_ = false;
      state_.Set("deactivated");
    }
    rb_vmo_fetched_ = false;
    delay_info_updated_ = false;
    expected_notifications_per_ring_.store(0);
    {
      fbl::AutoLock position_lock(&position_lock_);
      // Any pending LLCPP completer must be either replied to or closed before we destroy it.
      if (position_completer_.has_value()) {
        zxlogf(ERROR, "Position completer is still open when deactivating ring buffer channel");
        position_completer_->Close(ZX_ERR_INTERNAL);
      }
      position_completer_.reset();
    }
    rb_channel_ = nullptr;
  }
}

void SimpleAudioStream::CreateRingBuffer(
    StreamChannel* channel, audio_fidl::wire::Format format,
    fidl::ServerEnd<audio_fidl::RingBuffer> ring_buffer,
    fidl::WireServer<audio_fidl::StreamConfig>::CreateRingBufferCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  zx::channel rb_channel_local;
  zx::channel rb_channel_remote;
  bool found_one = false;

  // On errors we call Close() and ring_buffer goes out of scope closing the channel.

  // Only the privileged stream channel is allowed to change the format.
  {
    fbl::AutoLock channel_lock(&channel_lock_);
    if (channel != stream_channel_.get()) {
      zxlogf(ERROR, "Unprivileged channel cannot set the format");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  auto pcm_format = format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      pcm_format.valid_bits_per_sample, 8 * pcm_format.bytes_per_sample);

  if (sample_format == 0) {
    zxlogf(ERROR, "Unsupported format: Invalid bits per sample (%u/%u)\n",
           pcm_format.valid_bits_per_sample, 8 * pcm_format.bytes_per_sample);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (pcm_format.sample_format == audio_fidl::wire::SampleFormat::kPcmFloat) {
    sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    if (pcm_format.valid_bits_per_sample != 32 || pcm_format.bytes_per_sample != 4) {
      zxlogf(ERROR, "Unsupported format: float format must be 4 byte, 32 valid-bits\n");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  if (pcm_format.sample_format == audio_fidl::wire::SampleFormat::kPcmUnsigned) {
    sample_format |= AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  }

  // Check the format for compatibility
  for (const auto& fmt : supported_formats_) {
    if (audio::utils::FormatIsCompatible(pcm_format.frame_rate,
                                         static_cast<uint16_t>(pcm_format.number_of_channels),
                                         sample_format, fmt.range)) {
      found_one = true;
      break;
    }
  }

  if (!found_one) {
    zxlogf(ERROR, "Could not find a suitable format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Determine the frame size.
  frame_size_ = audio::utils::ComputeFrameSize(static_cast<uint16_t>(pcm_format.number_of_channels),
                                               sample_format);
  if (!frame_size_) {
    zxlogf(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)", pcm_format.number_of_channels,
           sample_format);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Looks like we are going ahead with this format change.  Tear down any
  // exiting ring buffer interface before proceeding.
  {
    fbl::AutoLock channel_lock(&channel_lock_);
    if (rb_channel_ != nullptr) {
      DeactivateRingBufferChannel(rb_channel_.get());
      ZX_DEBUG_ASSERT(rb_channel_ == nullptr);
    }
  }

  audio_stream_cmd_set_format_req_t req = {};
  req.frames_per_second = pcm_format.frame_rate;
  req.sample_format = sample_format;
  req.channels = static_cast<uint16_t>(pcm_format.number_of_channels);

  // Actually attempt to change the format.
  auto result = ChangeFormat(req);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to change the format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  uint32_t bytes_per_frame = static_cast<uint32_t>(pcm_format.bytes_per_sample) *
                             static_cast<uint32_t>(pcm_format.number_of_channels);
  if (pcm_format.frame_rate == 0) {
    zxlogf(ERROR, "Bad (zero) frame rate");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (bytes_per_frame == 0) {
    zxlogf(ERROR, "Bad (zero) frame size");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint32_t fifo_depth_frames = (fifo_depth_ + bytes_per_frame - 1) / bytes_per_frame;
  internal_delay_nsec_ = static_cast<uint64_t>(fifo_depth_frames) * 1'000'000'000 /
                         static_cast<uint64_t>(pcm_format.frame_rate);

  number_of_channels_.Set(pcm_format.number_of_channels);
  frame_rate_.Set(pcm_format.frame_rate);
  bits_per_slot_.Set(pcm_format.bytes_per_sample * 8);
  bits_per_sample_.Set(pcm_format.valid_bits_per_sample);
  using FidlSampleFormat = audio_fidl::wire::SampleFormat;
  // clang-format off
  switch (pcm_format.sample_format) {
    case FidlSampleFormat::kPcmSigned:   sample_format_.Set("PCM_signed");   break;
    case FidlSampleFormat::kPcmUnsigned: sample_format_.Set("PCM_unsigned"); break;
    case FidlSampleFormat::kPcmFloat:    sample_format_.Set("PCM_float");    break;
  }
  // clang-format on

  {
    fbl::AutoLock channel_lock(&channel_lock_);
    if (shutting_down_) {
      zxlogf(ERROR, "Already shutting down when trying to create ring buffer");
      return completer.Close(ZX_ERR_BAD_STATE);
    }

    rb_channel_ = Channel::Create<Channel>();

    fidl::OnUnboundFn<fidl::WireServer<audio_fidl::RingBuffer>> on_unbound =
        [this](fidl::WireServer<audio_fidl::RingBuffer>*, fidl::UnbindInfo info,
               fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer>) {
          // Do not log canceled cases which happens too often in particular in test cases.
          if (info.status() != ZX_ERR_CANCELED) {
            zxlogf(INFO, "Ring buffer channel closing: %s", info.FormatDescription().c_str());
          }
          ScopedToken t(domain_token());
          fbl::AutoLock channel_lock(&channel_lock_);
          this->DeactivateRingBufferChannel(rb_channel_.get());
        };

    fidl::BindServer<fidl::WireServer<audio_fidl::RingBuffer>>(dispatcher(), std::move(ring_buffer),
                                                               this, std::move(on_unbound));
  }
}

void SimpleAudioStream::WatchGainState(StreamChannel* channel,
                                       StreamChannel::WatchGainStateCompleter::Sync& completer) {
  ZX_ASSERT(!channel->gain_completer_);
  channel->gain_completer_ = completer.ToAsync();

  ScopedToken t(domain_token());
  // Reply is delayed if there is no change since the last reported gain state.
  if (channel->last_reported_gain_state_ != cur_gain_state_) {
    fidl::Arena allocator;
    audio_fidl::wire::GainState gain_state(allocator);
    if (cur_gain_state_.can_mute) {
      gain_state.set_muted(cur_gain_state_.cur_mute);
    }
    if (cur_gain_state_.can_agc) {
      gain_state.set_agc_enabled(cur_gain_state_.cur_agc);
    }
    gain_state.set_gain_db(cur_gain_state_.cur_gain);
    channel->last_reported_gain_state_ = cur_gain_state_;
    channel->gain_completer_->Reply(std::move(gain_state));
    channel->gain_completer_.reset();
  }
}

void SimpleAudioStream::WatchPlugState(StreamChannel* channel,
                                       StreamChannel::WatchPlugStateCompleter::Sync& completer) {
  ZX_ASSERT(!channel->plug_completer_);
  channel->plug_completer_ = completer.ToAsync();

  ScopedToken t(domain_token());
  bool plugged = pd_flags_ & AUDIO_PDNF_PLUGGED;
  // Reply is delayed if there is no change since the last reported plugged state.
  if (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kNotReported ||
      (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kPlugged) != plugged) {
    fidl::Arena allocator;
    audio_fidl::wire::PlugState plug_state(allocator);
    plug_state.set_plugged(plugged).set_plug_state_time(allocator, plug_time_);
    channel->last_reported_plugged_state_ =
        plugged ? StreamChannel::Plugged::kPlugged : StreamChannel::Plugged::kUnplugged;
    channel->plug_completer_->Reply(std::move(plug_state));
    channel->plug_completer_.reset();
  }
}

void SimpleAudioStream::WatchClockRecoveryPositionInfo(
    WatchClockRecoveryPositionInfoCompleter::Sync& completer) {
  fbl::AutoLock position_lock(&position_lock_);
  position_request_time_.Set(zx::clock::get_monotonic().get());
  position_completer_ = completer.ToAsync();
}

void SimpleAudioStream::WatchDelayInfo(WatchDelayInfoCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  if (!delay_info_updated_) {
    delay_info_updated_ = true;
    fidl::Arena allocator;
    auto delay_info = audio_fidl::wire::DelayInfo::Builder(allocator);
    delay_info.internal_delay(internal_delay_nsec_);
    delay_info.external_delay(external_delay_nsec_);
    completer.Reply(delay_info.Build());
  }
}

void SimpleAudioStream::SetGain(audio_fidl::wire::GainState target_state,
                                StreamChannel::SetGainCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  audio_stream_cmd_set_gain_req_t req = {};

  // Sanity check the request before passing it along
  if (target_state.has_muted() && target_state.muted() && !cur_gain_state_.can_mute) {
    zxlogf(ERROR, "Can't mute\n");
    return;
  }

  if (target_state.has_agc_enabled() && target_state.agc_enabled() && !cur_gain_state_.can_agc) {
    zxlogf(ERROR, "Can't enable AGC\n");
    return;
  }

  if (target_state.has_gain_db() && ((target_state.gain_db() < cur_gain_state_.min_gain) ||
                                     (target_state.gain_db() > cur_gain_state_.max_gain))) {
    zxlogf(ERROR, "Can't set gain outside valid range\n");
    return;
  }

  if (target_state.has_muted()) {
    req.flags |= AUDIO_SGF_MUTE_VALID;
    if (target_state.muted()) {
      req.flags |= AUDIO_SGF_MUTE;
    }
  }
  if (target_state.has_agc_enabled()) {
    req.flags |= AUDIO_SGF_AGC_VALID;
    if (target_state.agc_enabled()) {
      req.flags |= AUDIO_SGF_AGC;
    }
  }
  if (target_state.has_gain_db()) {
    req.flags |= AUDIO_SGF_GAIN_VALID;
    req.gain = target_state.gain_db();
  }
  auto status = SetGain(req);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not set gain state %d\n", status);
    return;
  }

  fbl::AutoLock channel_lock(&channel_lock_);
  for (auto& channel : stream_channels_) {
    if (channel.gain_completer_) {
      channel.gain_completer_->Reply(std::move(target_state));
      channel.gain_completer_.reset();
    }
  }
}

void SimpleAudioStream::SetActiveChannels(SetActiveChannelsRequestView request,
                                          SetActiveChannelsCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  zx_status_t status = ChangeActiveChannels(request->active_channels_bitmask);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error while setting the active channels");
    completer.ReplyError(status);
    return;
  }
  channels_to_use_bitmask_.Set(request->active_channels_bitmask);
  int64_t set_time = zx::clock::get_monotonic().get();
  completer.ReplySuccess(set_time);
}

void SimpleAudioStream::GetProperties(
    fidl::WireServer<audio_fidl::StreamConfig>::GetPropertiesCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  fidl::Arena allocator;
  audio_fidl::wire::StreamProperties stream_properties(allocator);
  stream_properties.set_unique_id(allocator);
  for (size_t i = 0; i < audio_fidl::wire::kUniqueIdSize; ++i) {
    stream_properties.unique_id().data_[i] = unique_id_.data[i];
  }

  fidl::StringView product(prod_name_, strlen(prod_name_));
  fidl::StringView manufacturer(mfr_name_, strlen(mfr_name_));

  stream_properties.set_is_input(is_input())
      .set_can_mute(cur_gain_state_.can_mute)
      .set_can_agc(cur_gain_state_.can_agc)
      .set_min_gain_db(cur_gain_state_.min_gain)
      .set_max_gain_db(cur_gain_state_.max_gain)
      .set_gain_step_db(cur_gain_state_.gain_step)
      .set_product(allocator, std::move(product))
      .set_manufacturer(allocator, std::move(manufacturer))
      .set_clock_domain(clock_domain_);

  if (pd_flags_ & AUDIO_PDNF_CAN_NOTIFY) {
    stream_properties.set_plug_detect_capabilities(
        audio_fidl::wire::PlugDetectCapabilities::kCanAsyncNotify);
  } else if (pd_flags_ & AUDIO_PDNF_HARDWIRED) {
    stream_properties.set_plug_detect_capabilities(
        audio_fidl::wire::PlugDetectCapabilities::kHardwired);
  }

  completer.Reply(std::move(stream_properties));
}

void SimpleAudioStream::GetSupportedFormats(
    fidl::WireServer<audio_fidl::StreamConfig>::GetSupportedFormatsCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  bool ranges_with_one_number_of_channels = true;
  // Build formats compatible with FIDL from a vector of audio_stream_format_range_t.
  // Needs to be alive until the reply is sent.
  struct FidlCompatibleFormats {
    fbl::Vector<uint8_t> number_of_channels;
    fbl::Vector<audio_fidl::wire::SampleFormat> sample_formats;
    fbl::Vector<uint32_t> frame_rates;
    fbl::Vector<uint8_t> valid_bits_per_sample;
    fbl::Vector<uint8_t> bytes_per_sample;
    fbl::Vector<FrequencyRange> frequency_ranges;
  };
  fbl::Vector<FidlCompatibleFormats> fidl_compatible_formats;
  for (SupportedFormat& i : supported_formats_) {
    std::vector<utils::Format> formats = audio::utils::GetAllFormats(i.range.sample_formats);
    ZX_ASSERT(formats.size() >= 1);
    for (utils::Format& j : formats) {
      fbl::Vector<uint32_t> rates;
      // Ignore flags if min and max are equal.
      if (i.range.min_frames_per_second == i.range.max_frames_per_second) {
        rates.push_back(i.range.min_frames_per_second);
      } else {
        ZX_ASSERT(!(i.range.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS));
        audio::utils::FrameRateEnumerator enumerator(i.range);
        for (uint32_t rate : enumerator) {
          rates.push_back(rate);
        }
      }

      fbl::Vector<uint8_t> number_of_channels;
      for (uint8_t k = i.range.min_channels; k <= i.range.max_channels; ++k) {
        number_of_channels.push_back(k);
      }
      if (i.range.min_channels != i.range.max_channels) {
        ranges_with_one_number_of_channels = false;
      }

      const size_t n_frequencies = i.frequency_ranges.size();
      fbl::Vector<FrequencyRange> frequency_ranges;
      for (size_t k = 0; k < n_frequencies; ++k) {
        frequency_ranges.push_back({
            .min_frequency = i.frequency_ranges[k].min_frequency,
            .max_frequency = i.frequency_ranges[k].max_frequency,
        });
      }

      fidl_compatible_formats.push_back({
          .number_of_channels = std::move(number_of_channels),
          .sample_formats = {j.format},
          .frame_rates = std::move(rates),
          .valid_bits_per_sample = {j.valid_bits_per_sample},
          .bytes_per_sample = {j.bytes_per_sample},
          .frequency_ranges = std::move(frequency_ranges),
      });
    }
  }

  fidl::Arena allocator;
  fidl::VectorView<audio_fidl::wire::SupportedFormats> fidl_formats(allocator,
                                                                    fidl_compatible_formats.size());
  // Get FIDL PcmSupportedFormats from FIDL compatible vectors.
  // Needs to be alive until the reply is sent.
  for (size_t i = 0; i < fidl_compatible_formats.size(); ++i) {
    FidlCompatibleFormats& src = fidl_compatible_formats[i];
    audio_fidl::wire::PcmSupportedFormats formats;
    formats.Allocate(allocator);
    fidl::VectorView<audio_fidl::wire::ChannelSet> channel_sets(allocator,
                                                                src.number_of_channels.size());

    for (uint8_t j = 0; j < src.number_of_channels.size(); ++j) {
      fidl::VectorView<audio_fidl::wire::ChannelAttributes> attributes(allocator,
                                                                       src.number_of_channels[j]);
      if (src.frequency_ranges.size()) {
        ZX_ASSERT_MSG(ranges_with_one_number_of_channels,
                      "must have only one number_of_channels for frequency ranges usage");
        for (uint8_t k = 0; k < src.number_of_channels[j]; ++k) {
          attributes[k].Allocate(allocator);
          attributes[k].set_min_frequency(src.frequency_ranges[k].min_frequency);
          attributes[k].set_max_frequency(src.frequency_ranges[k].max_frequency);
        }
      }
      channel_sets[j].Allocate(allocator);
      channel_sets[j].set_attributes(allocator, std::move(attributes));
    }
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

// Ring Buffer GetProperties.
void SimpleAudioStream::GetProperties(GetPropertiesCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  fidl::Arena allocator;
  audio_fidl::wire::RingBufferProperties ring_buffer_properties(allocator);
  ring_buffer_properties.set_fifo_depth(fifo_depth_)
      .set_external_delay(allocator, external_delay_nsec_)
      .set_needs_cache_flush_or_invalidate(true)
      .set_turn_on_delay(allocator, turn_on_delay_nsec_);
  completer.Reply(std::move(ring_buffer_properties));
}

void SimpleAudioStream::GetVmo(GetVmoRequestView request, GetVmoCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  frames_requested_.Set(request->min_frames);

  if (rb_started_) {
    zxlogf(ERROR, "Cannot retrieve the buffer if already started");
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }
  expected_notifications_per_ring_.store(request->clock_recovery_notifications_per_ring);

  uint32_t num_ring_buffer_frames = 0;
  audio_proto::RingBufGetBufferReq req = {};
  req.min_ring_buffer_frames = request->min_frames;
  req.notifications_per_ring = request->clock_recovery_notifications_per_ring;
  zx::vmo buffer;
  auto status = GetBuffer(req, &num_ring_buffer_frames, &buffer);
  if (status != ZX_OK) {
    expected_notifications_per_ring_.store(0);
    zxlogf(ERROR, "Failed to retrieve the buffer (err %u)", status);
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }

  uint64_t size = 0;
  status = buffer.get_size(&size);
  if (status != ZX_OK) {
    expected_notifications_per_ring_.store(0);
    zxlogf(ERROR, "Failed to get the size of the VMO (err %u)", status);
    completer.ReplyError(audio_fidl::wire::GetVmoError::kInternalError);
    return;
  }
  rb_vmo_fetched_ = true;
  ring_buffer_size_.Set(size);
  completer.ReplySuccess(num_ring_buffer_frames, std::move(buffer));
}

void SimpleAudioStream::Start(StartCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  if (!rb_vmo_fetched_) {
    zxlogf(ERROR, "Cannot start the ring buffer before retrieving the VMO");
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }
  if (rb_started_) {
    zxlogf(ERROR, "Cannot start the ring buffer if already started");
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  uint64_t start_time = 0;
  auto result = Start(&start_time);
  if (result == ZX_OK) {
    rb_started_ = true;
    state_.Set("started");
    start_time_.Set(zx::clock::get_monotonic().get());
  }
  completer.Reply(start_time);
}

void SimpleAudioStream::Stop(StopCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  if (!rb_vmo_fetched_) {
    zxlogf(ERROR, "Cannot stop the ring buffer before retrieving the VMO");
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }
  if (!rb_started_) {
    zxlogf(INFO, "Stop called while stopped; doing nothing");
    completer.Reply();
    return;
  }

  auto result = Stop();
  if (result == ZX_OK) {
    rb_started_ = false;
    state_.Set("stopped");
  }
  completer.Reply();
}

}  // namespace audio
