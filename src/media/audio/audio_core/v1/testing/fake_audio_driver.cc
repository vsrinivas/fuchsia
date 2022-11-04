// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"

#include <lib/syslog/cpp/macros.h>

#include <audio-proto-utils/format-utils.h>
#include <gtest/gtest.h>

namespace media::audio::testing {

FakeAudioDriver::FakeAudioDriver(zx::channel channel, async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher), stream_binding_(this, std::move(channel), dispatcher) {
  fuchsia::hardware::audio::ChannelSet channel_set = {};
  constexpr size_t kSupportedNumberOfChannels = 2;
  std::vector<fuchsia::hardware::audio::ChannelAttributes> attributes(kSupportedNumberOfChannels);
  channel_set.set_attributes(std::move(attributes));
  formats_.mutable_channel_sets()->push_back(std::move(channel_set));
  formats_.mutable_sample_formats()->push_back(fuchsia::hardware::audio::SampleFormat::PCM_SIGNED);
  formats_.mutable_bytes_per_sample()->push_back(2);
  formats_.mutable_valid_bits_per_sample()->push_back(16);
  formats_.mutable_frame_rates()->push_back(48000);
  Stop();
}

void FakeAudioDriver::Start() {
  assert(!stream_binding_.is_bound());
  stream_binding_.Bind(std::move(stream_req_), dispatcher_);
  if (ring_buffer_binding_.has_value() && !ring_buffer_binding_->is_bound()) {
    ring_buffer_binding_->Bind(std::move(ring_buffer_req_), dispatcher_);
  }
}

void FakeAudioDriver::Stop() {
  if (stream_binding_.is_bound()) {
    stream_req_ = stream_binding_.Unbind();
  }
  if (ring_buffer_binding_.has_value() && ring_buffer_binding_->is_bound()) {
    ring_buffer_req_ = ring_buffer_binding_->Unbind();
  }
}

fzl::VmoMapper FakeAudioDriver::CreateRingBuffer(size_t size) {
  FX_CHECK(!ring_buffer_) << "Calling CreateRingBuffer multiple times is not supported";

  ring_buffer_size_ = size;
  fzl::VmoMapper mapper;
  mapper.CreateAndMap(ring_buffer_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                      &ring_buffer_);
  return mapper;
}

void FakeAudioDriver::GetProperties(
    fuchsia::hardware::audio::StreamConfig::GetPropertiesCallback callback) {
  fuchsia::hardware::audio::StreamProperties props = {};

  std::memcpy(props.mutable_unique_id()->data(), uid_.data, sizeof(uid_.data));
  props.set_manufacturer(manufacturer_);
  props.set_product(product_);
  props.set_can_mute(can_mute_);
  props.set_can_agc(can_agc_);
  props.set_min_gain_db(gain_limits_.first);
  props.set_max_gain_db(gain_limits_.second);
  props.set_gain_step_db(0.001f);
  props.set_plug_detect_capabilities(
      fuchsia::hardware::audio::PlugDetectCapabilities::CAN_ASYNC_NOTIFY);
  props.set_clock_domain(clock_domain_);

  callback(std::move(props));
}

void FakeAudioDriver::GetSupportedFormats(
    fuchsia::hardware::audio::StreamConfig::GetSupportedFormatsCallback callback) {
  fuchsia::hardware::audio::SupportedFormats formats = {};
  formats.set_pcm_supported_formats(std::move(formats_));

  std::vector<fuchsia::hardware::audio::SupportedFormats> all_formats = {};
  all_formats.push_back(std::move(formats));
  callback(std::move(all_formats));
}

void FakeAudioDriver::CreateRingBuffer(
    fuchsia::hardware::audio::Format format,
    ::fidl::InterfaceRequest<fuchsia::hardware::audio::RingBuffer> ring_buffer) {
  ring_buffer_binding_.emplace(this, ring_buffer.TakeChannel(), dispatcher_);
  selected_format_ = format.pcm_format();
}

void FakeAudioDriver::WatchGainState(
    fuchsia::hardware::audio::StreamConfig::WatchGainStateCallback callback) {
  if (gain_state_sent_) {
    return;  // Only send gain state once, as if it never changed.
  }
  gain_state_sent_ = true;
  fuchsia::hardware::audio::GainState gain_state = {};
  gain_state.set_muted(cur_mute_);
  gain_state.set_agc_enabled(cur_agc_);
  gain_state.set_gain_db(cur_gain_);
  callback(std::move(gain_state));
}

void FakeAudioDriver::SetGain(fuchsia::hardware::audio::GainState target_state) {}

void FakeAudioDriver::WatchPlugState(
    fuchsia::hardware::audio::StreamConfig::WatchPlugStateCallback callback) {
  if (plug_state_sent_) {
    return;  // Only send plug state once, as if it never changed.
  }
  plug_state_sent_ = true;
  fuchsia::hardware::audio::PlugState plug_state = {};
  plug_state.set_plugged(true);
  plug_state.set_plug_state_time(0);
  callback(std::move(plug_state));
}

void FakeAudioDriver::GetProperties(
    fuchsia::hardware::audio::RingBuffer::GetPropertiesCallback callback) {
  fuchsia::hardware::audio::RingBufferProperties props = {};

  if (external_delay_.has_value()) {
    props.set_external_delay(external_delay_->to_nsecs());
  }
  if (fifo_depth_.has_value()) {
    props.set_fifo_depth(*fifo_depth_);
  }
  props.set_needs_cache_flush_or_invalidate(false);

  callback(std::move(props));
}

void FakeAudioDriver::SendPositionNotification(zx::time timestamp, uint32_t position) {
  position_notify_timestamp_mono_ = timestamp;
  position_notify_position_bytes_ = position;

  position_notification_values_are_set_ = true;
  if (position_notify_callback_) {
    PositionNotification();
  }
}

void FakeAudioDriver::WatchClockRecoveryPositionInfo(
    fuchsia::hardware::audio::RingBuffer::WatchClockRecoveryPositionInfoCallback callback) {
  position_notify_callback_ = std::move(callback);

  if (position_notification_values_are_set_) {
    PositionNotification();
  }
}

void FakeAudioDriver::PositionNotification() {
  FX_CHECK(position_notify_callback_);
  FX_CHECK(position_notification_values_are_set_);

  // Real audio drivers can't emit position notifications until started; we shouldn't either
  if (is_running_) {
    // Clear both prerequisites for sending this notification
    position_notification_values_are_set_ = false;
    auto callback = *std::move(position_notify_callback_);
    position_notify_callback_ = std::nullopt;

    fuchsia::hardware::audio::RingBufferPositionInfo info{
        .timestamp = position_notify_timestamp_mono_.get(),
        .position = position_notify_position_bytes_,
    };

    callback(std::move(info));
  }
}

void FakeAudioDriver::GetVmo(uint32_t min_frames, uint32_t clock_recovery_notifications_per_ring,
                             fuchsia::hardware::audio::RingBuffer::GetVmoCallback callback) {
  // This should be true since it's set as part of creating the channel that's carrying these
  // messages.
  FX_CHECK(selected_format_);

  if (!ring_buffer_) {
    // If we haven't created a ring buffer, we'll just drop this request.
    return;
  }
  FX_CHECK(ring_buffer_);

  // Dup our ring buffer VMO to send over the channel.
  zx::vmo dup;
  FX_CHECK(ring_buffer_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) == ZX_OK);

  // Compute the buffer size in frames.
  auto frame_size = selected_format_->number_of_channels * selected_format_->bytes_per_sample;
  auto ring_buffer_frames = ring_buffer_size_ / frame_size;

  fuchsia::hardware::audio::RingBuffer_GetVmo_Result result = {};
  fuchsia::hardware::audio::RingBuffer_GetVmo_Response response = {};
  response.num_frames = static_cast<uint32_t>(ring_buffer_frames);
  response.ring_buffer = std::move(dup);
  result.set_response(std::move(response));
  callback(std::move(result));
}

void FakeAudioDriver::Start(fuchsia::hardware::audio::RingBuffer::StartCallback callback) {
  EXPECT_TRUE(!is_running_);
  mono_start_time_ = async::Now(dispatcher_);
  is_running_ = true;

  callback(mono_start_time_.get());
}

void FakeAudioDriver::Stop(fuchsia::hardware::audio::RingBuffer::StopCallback callback) {
  EXPECT_TRUE(is_running_);
  is_running_ = false;

  position_notify_callback_ = std::nullopt;
  position_notification_values_are_set_ = false;

  callback();
}

void FakeAudioDriver::WatchDelayInfo(
    fuchsia::hardware::audio::RingBuffer::WatchDelayInfoCallback callback) {
  if (delay_info_sent_) {
    return;  // Only send delay state once, as if it never changed.
  }
  delay_info_sent_ = true;
  callback({});
}

}  // namespace media::audio::testing
