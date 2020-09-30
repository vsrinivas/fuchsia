// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/device/audio.h>

#include <limits>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
namespace audio {

void SimpleAudioStream::Shutdown() {
  if (!shutting_down_.exchange(true)) {
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
      zxlogf(ERROR, "Init failure in %s (res %d)", __PRETTY_FUNCTION__, res);
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
    zxlogf(ERROR, "Publish failure in %s (res %d)", __PRETTY_FUNCTION__, res);
    return res;
  }

  return ZX_OK;
}

zx_status_t SimpleAudioStream::PublishInternal() {
  device_name_[sizeof(device_name_) - 1] = 0;
  if (!strlen(device_name_)) {
    zxlogf(ERROR, "Zero-length device name in %s", __PRETTY_FUNCTION__);
    return ZX_ERR_BAD_STATE;
  }

  // If we succeed in adding our device, add an explicit reference to
  // ourselves to represent the reference now being held by the DDK.  We will
  // get this reference back when the DDK (eventually) calls release.
  zx_status_t res = DdkAdd(device_name_);
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
  auto builder = audio_fidl::PlugState::UnownedBuilder();
  fidl::aligned<bool> plugged = pd_flags_ & AUDIO_PDNF_PLUGGED;
  builder.set_plugged(fidl::unowned_ptr(&plugged));
  builder.set_plug_state_time(fidl::unowned_ptr(&plug_time_));

  fbl::AutoLock channel_lock(&channel_lock_);
  for (auto& channel : stream_channels_) {
    if (channel.plug_completer_) {
      channel.plug_completer_->Reply(builder.build());
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
  audio_fidl::RingBufferPositionInfo position_info = {};
  position_info.position = notif.ring_buffer_pos;
  position_info.timestamp = notif.monotonic_time;

  fbl::AutoLock position_lock(&position_lock_);
  if (position_completer_) {
    position_completer_->Reply(position_info);
    position_completer_.reset();
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

void SimpleAudioStream::GetChannel(GetChannelCompleter::Sync& completer) {
  fbl::AutoLock channel_lock(&channel_lock_);
  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have an stream_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  bool privileged = (stream_channel_ == nullptr);

  zx::channel stream_channel_local;
  zx::channel stream_channel_remote;
  auto status = zx::channel::create(0, &stream_channel_local, &stream_channel_remote);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create channel in %s", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }

  auto stream_channel = StreamChannel::Create<StreamChannel>(this);
  // We keep alive all channels in stream_channels_ (protected by channel_lock_).
  stream_channels_.push_back(stream_channel);
  fidl::OnUnboundFn<audio_fidl::StreamConfig::Interface> on_unbound =
      [this, stream_channel](audio_fidl::StreamConfig::Interface*, fidl::UnbindInfo, zx::channel) {
        ScopedToken t(domain_token());
        fbl::AutoLock channel_lock(&channel_lock_);
        this->DeactivateStreamChannel(stream_channel.get());
      };

  fidl::BindServer<audio_fidl::StreamConfig::Interface>(
      dispatcher(), std::move(stream_channel_local), stream_channel.get(), std::move(on_unbound));

  if (privileged) {
    ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
    stream_channel_ = stream_channel;
  }
  completer.Reply(std::move(stream_channel_remote));
}

void SimpleAudioStream::DeactivateStreamChannel(StreamChannel* channel) {
  if (stream_channel_.get() == channel) {
    stream_channel_ = nullptr;
  }

  // Any pending LLCPP completer must be either replied to or closed before we destroy it.
  if (channel->plug_completer_.has_value()) {
    channel->plug_completer_->Close(ZX_ERR_INTERNAL);
  }
  channel->plug_completer_.reset();

  // Any pending LLCPP completer must be either replied to or closed before we destroy it.
  if (channel->gain_completer_.has_value()) {
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
    }
    rb_fetched_ = false;
    expected_notifications_per_ring_.store(0);
    {
      fbl::AutoLock position_lock(&position_lock_);
      // Any pending LLCPP completer must be either replied to or closed before we destroy it.
      if (position_completer_.has_value()) {
        position_completer_->Close(ZX_ERR_INTERNAL);
      }
      position_completer_.reset();
    }
    rb_channel_ = nullptr;
  }
}

void SimpleAudioStream::CreateRingBuffer(
    StreamChannel* channel, audio_fidl::Format format, zx::channel ring_buffer,
    audio_fidl::StreamConfig::Interface::CreateRingBufferCompleter::Sync& completer) {
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

  auto format_v1 = format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      format_v1.valid_bits_per_sample, 8 * format_v1.bytes_per_sample);

  if (sample_format == 0) {
    zxlogf(ERROR, "Unsupported format: Invalid bits per sample (%u/%u)\n",
           format_v1.valid_bits_per_sample, 8 * format_v1.bytes_per_sample);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (format_v1.sample_format == audio_fidl::SampleFormat::PCM_FLOAT) {
    sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    if (format_v1.valid_bits_per_sample != 32 || format_v1.bytes_per_sample != 4) {
      zxlogf(ERROR, "Unsupported format: Not 32 per sample/channel for float\n");
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  if (format_v1.sample_format == audio_fidl::SampleFormat::PCM_UNSIGNED) {
    sample_format |= AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  }

  // Check the format for compatibility
  for (const auto& fmt : supported_formats_) {
    if (audio::utils::FormatIsCompatible(format_v1.frame_rate,
                                         static_cast<uint16_t>(format_v1.number_of_channels),
                                         sample_format, fmt)) {
      found_one = true;
      break;
    }
  }

  if (!found_one) {
    zxlogf(ERROR, "Could not find a suitable format in %s", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Determine the frame size.
  frame_size_ = audio::utils::ComputeFrameSize(static_cast<uint16_t>(format_v1.number_of_channels),
                                               sample_format);
  if (!frame_size_) {
    zxlogf(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)", format_v1.number_of_channels,
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
  req.frames_per_second = format_v1.frame_rate;
  req.sample_format = sample_format;
  req.channels = static_cast<uint16_t>(format_v1.number_of_channels);
  req.channels_to_use_bitmask = format_v1.channels_to_use_bitmask;

  // Actually attempt to change the format.
  auto result = ChangeFormat(req);
  if (result != ZX_OK) {
    zxlogf(ERROR, "Could not ChangeFormat in %s", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  {
    fbl::AutoLock channel_lock(&channel_lock_);
    rb_channel_ = Channel::Create<Channel>();

    fidl::OnUnboundFn<audio_fidl::RingBuffer::Interface> on_unbound =
        [this](audio_fidl::RingBuffer::Interface*, fidl::UnbindInfo, zx::channel) {
          ScopedToken t(domain_token());
          fbl::AutoLock channel_lock(&channel_lock_);
          this->DeactivateRingBufferChannel(rb_channel_.get());
        };

    fidl::BindServer<audio_fidl::RingBuffer::Interface>(dispatcher(), std::move(ring_buffer), this,
                                                        std::move(on_unbound));
  }
}

void SimpleAudioStream::WatchGainState(StreamChannel* channel,
                                       StreamChannel::WatchGainStateCompleter::Sync& completer) {
  ZX_ASSERT(!channel->gain_completer_);
  channel->gain_completer_ = completer.ToAsync();

  ScopedToken t(domain_token());
  // Reply is delayed if there is no change since the last reported gain state.
  // TODO(andresoportus): Create a type with default <=> in C++20, or just defined ==.
  if (channel->last_reported_gain_state_.cur_gain != cur_gain_state_.cur_gain ||
      channel->last_reported_gain_state_.cur_mute != cur_gain_state_.cur_mute ||
      channel->last_reported_gain_state_.cur_agc != cur_gain_state_.cur_agc ||
      channel->last_reported_gain_state_.can_mute != cur_gain_state_.can_mute ||
      channel->last_reported_gain_state_.can_agc != cur_gain_state_.can_agc ||
      channel->last_reported_gain_state_.min_gain != cur_gain_state_.min_gain ||
      channel->last_reported_gain_state_.max_gain != cur_gain_state_.max_gain ||
      channel->last_reported_gain_state_.gain_step != cur_gain_state_.gain_step) {
    auto builder = audio_fidl::GainState::UnownedBuilder();
    fidl::aligned<bool> mute = cur_gain_state_.cur_mute;
    fidl::aligned<bool> agc = cur_gain_state_.cur_agc;
    fidl::aligned<float> gain = cur_gain_state_.cur_gain;
    if (cur_gain_state_.can_mute) {
      builder.set_muted(fidl::unowned_ptr(&mute));
    }
    if (cur_gain_state_.can_agc) {
      builder.set_agc_enabled(fidl::unowned_ptr(&agc));
    }
    builder.set_gain_db(fidl::unowned_ptr(&gain));
    channel->last_reported_gain_state_ = cur_gain_state_;
    channel->gain_completer_->Reply(builder.build());
    channel->gain_completer_.reset();
  }
}

void SimpleAudioStream::WatchPlugState(StreamChannel* channel,
                                       StreamChannel::WatchPlugStateCompleter::Sync& completer) {
  ZX_ASSERT(!channel->plug_completer_);
  channel->plug_completer_ = completer.ToAsync();

  ScopedToken t(domain_token());
  fidl::aligned<bool> plugged = pd_flags_ & AUDIO_PDNF_PLUGGED;
  // Reply is delayed if there is no change since the last reported plugged state.
  if (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kNotReported ||
      (channel->last_reported_plugged_state_ == StreamChannel::Plugged::kPlugged) != plugged) {
    auto builder = audio_fidl::PlugState::UnownedBuilder();
    builder.set_plugged(fidl::unowned_ptr(&plugged));
    fidl::aligned<zx_time_t> plug_time = plug_time_;
    builder.set_plug_state_time(fidl::unowned_ptr(&plug_time));
    channel->last_reported_plugged_state_ =
        plugged ? StreamChannel::Plugged::kPlugged : StreamChannel::Plugged::kUnplugged;
    channel->plug_completer_->Reply(builder.build());
    channel->plug_completer_.reset();
  }
}

void SimpleAudioStream::WatchClockRecoveryPositionInfo(
    WatchClockRecoveryPositionInfoCompleter::Sync& completer) {
  fbl::AutoLock position_lock(&position_lock_);
  position_completer_ = completer.ToAsync();
}

void SimpleAudioStream::SetGain(audio_fidl::GainState target_state,
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

void SimpleAudioStream::GetProperties(
    audio_fidl::StreamConfig::Interface::GetPropertiesCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  auto builder = audio_fidl::StreamProperties::UnownedBuilder();
  fidl::Array<uint8_t, audio_fidl::UNIQUE_ID_SIZE> unique_id = {};
  for (size_t i = 0; i < audio_fidl::UNIQUE_ID_SIZE; ++i) {
    unique_id.data_[i] = unique_id_.data[i];
  }
  fidl::aligned<fidl::Array<uint8_t, audio_fidl::UNIQUE_ID_SIZE>> aligned_unique_id = unique_id;
  builder.set_unique_id(fidl::unowned_ptr(&aligned_unique_id));
  fidl::aligned<bool> input = is_input();
  builder.set_is_input(fidl::unowned_ptr(&input));
  fidl::aligned<bool> mute = cur_gain_state_.can_mute;
  builder.set_can_mute(fidl::unowned_ptr(&mute));
  fidl::aligned<bool> agc = cur_gain_state_.can_agc;
  builder.set_can_agc(fidl::unowned_ptr(&agc));
  fidl::aligned<float> min_gain = cur_gain_state_.min_gain;
  builder.set_min_gain_db(fidl::unowned_ptr(&min_gain));
  fidl::aligned<float> max_gain = cur_gain_state_.max_gain;
  builder.set_max_gain_db(fidl::unowned_ptr(&max_gain));
  fidl::aligned<float> gain_step = cur_gain_state_.gain_step;
  builder.set_gain_step_db(fidl::unowned_ptr(&gain_step));
  fidl::StringView product(prod_name_, strlen(prod_name_));
  builder.set_product(fidl::unowned_ptr(&product));
  fidl::StringView manufacturer(mfr_name_, strlen(mfr_name_));
  builder.set_manufacturer(fidl::unowned_ptr(&manufacturer));
  fidl::aligned<uint32_t> domain = clock_domain_;
  builder.set_clock_domain(fidl::unowned_ptr(&domain));
  fidl::aligned<audio_fidl::PlugDetectCapabilities> cap;
  if (pd_flags_ & AUDIO_PDNF_CAN_NOTIFY) {
    cap = audio_fidl::PlugDetectCapabilities::CAN_ASYNC_NOTIFY;
    builder.set_plug_detect_capabilities(fidl::unowned_ptr(&cap));
  } else if (pd_flags_ & AUDIO_PDNF_HARDWIRED) {
    cap = audio_fidl::PlugDetectCapabilities::HARDWIRED;
    builder.set_plug_detect_capabilities(fidl::unowned_ptr(&cap));
  }
  completer.Reply(builder.build());
}

void SimpleAudioStream::GetSupportedFormats(
    audio_fidl::StreamConfig::Interface::GetSupportedFormatsCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  // Build formats compatible with FIDL from a vector of audio_stream_format_range_t.
  // Needs to be alive until the reply is sent.
  struct FidlCompatibleFormats {
    fbl::Vector<uint8_t> number_of_channels;
    fbl::Vector<audio_fidl::SampleFormat> sample_formats;
    fbl::Vector<uint32_t> frame_rates;
    fbl::Vector<uint8_t> valid_bits_per_sample;
    fbl::Vector<uint8_t> bytes_per_sample;
  };
  fbl::Vector<FidlCompatibleFormats> fidl_compatible_formats;
  for (auto& i : supported_formats_) {
    audio_fidl::SampleFormat sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
    ZX_ASSERT(!(i.sample_formats & AUDIO_SAMPLE_FORMAT_BITSTREAM));
    ZX_ASSERT(!(i.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_INVERT_ENDIAN));

    if (i.sample_formats & AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED) {
      sample_format = audio_fidl::SampleFormat::PCM_UNSIGNED;
    }

    auto noflag_format =
        static_cast<audio_sample_format_t>(i.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK);

    auto sizes = audio::utils::GetSampleSizes(noflag_format);

    ZX_ASSERT(sizes.valid_bits_per_sample != 0);
    ZX_ASSERT(sizes.bytes_per_sample != 0);

    if (noflag_format == AUDIO_SAMPLE_FORMAT_32BIT_FLOAT) {
      sample_format = audio_fidl::SampleFormat::PCM_FLOAT;
    }

    fbl::Vector<uint32_t> rates;
    // Ignore flags if min and max are equal.
    if (i.min_frames_per_second == i.max_frames_per_second) {
      rates.push_back(i.min_frames_per_second);
    } else {
      ZX_ASSERT(!(i.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS));
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
        {sample_format},
        std::move(rates),
        {sizes.valid_bits_per_sample},
        {sizes.bytes_per_sample},
    });
  }

  // Get FIDL PcmSupportedFormats from FIDL compatible vectors.
  // Needs to be alive until the reply is sent.
  fbl::Vector<audio_fidl::PcmSupportedFormats> fidl_pcm_formats;
  for (auto& i : fidl_compatible_formats) {
    audio_fidl::PcmSupportedFormats formats;
    formats.number_of_channels = ::fidl::VectorView<uint8_t>(
        fidl::unowned_ptr(i.number_of_channels.data()), i.number_of_channels.size());
    formats.sample_formats = ::fidl::VectorView<audio_fidl::SampleFormat>(
        fidl::unowned_ptr(i.sample_formats.data()), i.sample_formats.size());
    formats.frame_rates =
        ::fidl::VectorView<uint32_t>(fidl::unowned_ptr(i.frame_rates.data()), i.frame_rates.size());
    formats.bytes_per_sample = ::fidl::VectorView<uint8_t>(
        fidl::unowned_ptr(i.bytes_per_sample.data()), i.bytes_per_sample.size());
    formats.valid_bits_per_sample = ::fidl::VectorView<uint8_t>(
        fidl::unowned_ptr(i.valid_bits_per_sample.data()), i.valid_bits_per_sample.size());
    fidl_pcm_formats.push_back(std::move(formats));
  }

  // Get builders from PcmSupportedFormats tables.
  // Needs to be alive until the reply is sent.
  fbl::Vector<audio_fidl::SupportedFormats::UnownedBuilder> fidl_builders;
  for (auto& i : fidl_pcm_formats) {
    auto builder = audio_fidl::SupportedFormats::UnownedBuilder();
    builder.set_pcm_supported_formats(fidl::unowned_ptr(&i));
    fidl_builders.push_back(std::move(builder));
  }

  // Build FIDL SupportedFormats from PcmSupportedFormats's builders.
  // Needs to be alive until the reply is sent.
  fbl::Vector<audio_fidl::SupportedFormats> fidl_formats;
  for (auto& i : fidl_builders) {
    fidl_formats.push_back(i.build());
  }

  completer.Reply(::fidl::VectorView<audio_fidl::SupportedFormats>(
      fidl::unowned_ptr(fidl_formats.data()), fidl_formats.size()));
}

// Ring Buffer GetProperties.
void SimpleAudioStream::GetProperties(GetPropertiesCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  auto builder = audio_fidl::RingBufferProperties::UnownedBuilder();
  fidl::aligned<uint32_t> fifo_depth = fifo_depth_;
  builder.set_fifo_depth(fidl::unowned_ptr(&fifo_depth));
  fidl::aligned<int64_t> delay = static_cast<int64_t>(external_delay_nsec_);
  builder.set_external_delay(fidl::unowned_ptr(&delay));
  fidl::aligned<bool> flush = true;
  builder.set_needs_cache_flush_or_invalidate(fidl::unowned_ptr(&flush));
  completer.Reply(builder.build());
}

void SimpleAudioStream::GetVmo(uint32_t min_frames, uint32_t notifications_per_ring,
                               GetVmoCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  if (rb_started_) {
    completer.ReplyError(audio_fidl::GetVmoError::INTERNAL_ERROR);
    return;
  }

  uint32_t num_ring_buffer_frames = 0;
  audio_proto::RingBufGetBufferReq req = {};
  req.min_ring_buffer_frames = min_frames;
  req.notifications_per_ring = notifications_per_ring;
  zx::vmo buffer;
  auto status = GetBuffer(req, &num_ring_buffer_frames, &buffer);
  if (status != ZX_OK) {
    expected_notifications_per_ring_.store(0);
    completer.ReplyError(audio_fidl::GetVmoError::INTERNAL_ERROR);
    return;
  }

  expected_notifications_per_ring_.store(req.notifications_per_ring);
  rb_fetched_ = true;
  completer.ReplySuccess(num_ring_buffer_frames, std::move(buffer));
}

void SimpleAudioStream::Start(StartCompleter::Sync& completer) {
  ScopedToken t(domain_token());

  uint64_t start_time = 0;
  if (rb_started_ || !rb_fetched_) {
    zxlogf(ERROR, "Could not start %s\n", __PRETTY_FUNCTION__);
    completer.Reply(start_time);
    return;
  }

  auto result = Start(&start_time);
  if (result == ZX_OK) {
    rb_started_ = true;
  }
  completer.Reply(start_time);
}

void SimpleAudioStream::Stop(StopCompleter::Sync& completer) {
  ScopedToken t(domain_token());
  if (!rb_started_) {
    zxlogf(ERROR, "Could not stop %s\n", __PRETTY_FUNCTION__);
    completer.Reply();
    return;
  }

  auto result = Stop();
  if (result == ZX_OK) {
    rb_started_ = false;
  }
  completer.Reply();
}

}  // namespace audio
