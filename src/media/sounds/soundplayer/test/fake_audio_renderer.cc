// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/test/fake_audio_renderer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <iostream>
#include <limits>

#include <gtest/gtest.h>

namespace soundplayer {
namespace test {

FakeAudioRenderer::FakeAudioRenderer() : binding_(this) {}

FakeAudioRenderer::~FakeAudioRenderer() {}

void FakeAudioRenderer::Bind(fidl::InterfaceRequest<fuchsia::media::AudioRenderer> request,
                             fit::function<void(zx_status_t)> error_handler) {
  binding_.Bind(std::move(request));
  binding_.set_error_handler(
      [this, error_handler = std::move(error_handler)](zx_status_t status) mutable {
        // Setting the error handler to null deletes this fit::function and its captures, so we need
        // to move |error_handler| out of the captures.
        auto handler = std::move(error_handler);
        binding_.set_error_handler(nullptr);
        binding_.Unbind();
        handler(status);
      });
}

void FakeAudioRenderer::SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) {
  EXPECT_EQ(expectations_.stream_type_.sample_format, stream_type.sample_format);
  EXPECT_EQ(expectations_.stream_type_.channels, stream_type.channels);
  EXPECT_EQ(expectations_.stream_type_.frames_per_second, stream_type.frames_per_second);
  set_pcm_stream_type_called_ = true;
}

void FakeAudioRenderer::AddPayloadBuffer(uint32_t id, zx::vmo payload_buffer) {
  // All the expected packets should have the same payload buffer id. Check |id| against the
  // buffer id of the first expected packet.
  EXPECT_FALSE(expectations_.packets_.empty());
  EXPECT_EQ(expectations_.packets_[0].payload_buffer_id, id);

  if (expectations_.payload_buffer_ != ZX_KOID_INVALID) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(payload_buffer.get(), ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    FX_CHECK(status == ZX_OK);
    EXPECT_EQ(expectations_.payload_buffer_, info.koid);
  }

  add_payload_buffer_called_ = true;
}

void FakeAudioRenderer::RemovePayloadBuffer(uint32_t id) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::SetPtsUnits(uint32_t tick_per_second_numerator,
                                    uint32_t tick_per_second_denominator) {
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::SetPtsContinuityThreshold(float threshold_seconds) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::SetReferenceClock(zx::clock ref_clock) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::GetReferenceClock(GetReferenceClockCallback callback) {
  callback(zx::clock(ZX_HANDLE_INVALID));
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::SendPacket(fuchsia::media::StreamPacket packet,
                                   SendPacketCallback callback) {
  EXPECT_TRUE(set_usage_called_);
  EXPECT_TRUE(set_pcm_stream_type_called_);
  EXPECT_TRUE(add_payload_buffer_called_);
  EXPECT_FALSE(expected_packets_iterator_ == expectations_.packets_.end());

  auto& expected_packet = *expected_packets_iterator_;
  EXPECT_EQ(expected_packet.pts, packet.pts);
  EXPECT_EQ(expected_packet.payload_buffer_id, packet.payload_buffer_id);
  EXPECT_EQ(expected_packet.payload_offset, packet.payload_offset);
  EXPECT_EQ(expected_packet.payload_size, packet.payload_size);
  EXPECT_EQ(expected_packet.flags, packet.flags);
  EXPECT_EQ(expected_packet.buffer_config, packet.buffer_config);
  EXPECT_EQ(expected_packet.stream_segment_id, packet.stream_segment_id);

  // We should be done with packets now.
  ++expected_packets_iterator_;
  EXPECT_TRUE(expected_packets_iterator_ == expectations_.packets_.end());

  send_packet_callback_ = std::move(callback);
}

void FakeAudioRenderer::SendPacketNoReply(fuchsia::media::StreamPacket packet) {
  EXPECT_TRUE(set_usage_called_);
  EXPECT_TRUE(set_pcm_stream_type_called_);
  EXPECT_TRUE(add_payload_buffer_called_);
  EXPECT_FALSE(expected_packets_iterator_ == expectations_.packets_.end());

  auto& expected_packet = *expected_packets_iterator_;
  EXPECT_EQ(expected_packet.pts, packet.pts);
  EXPECT_EQ(expected_packet.payload_buffer_id, packet.payload_buffer_id);
  EXPECT_EQ(expected_packet.payload_offset, packet.payload_offset);
  EXPECT_EQ(expected_packet.payload_size, packet.payload_size);
  EXPECT_EQ(expected_packet.flags, packet.flags);
  EXPECT_EQ(expected_packet.buffer_config, packet.buffer_config);
  EXPECT_EQ(expected_packet.stream_segment_id, packet.stream_segment_id);

  // This should not be the last packet. The last packet is sent using |SendPacket|.
  ++expected_packets_iterator_;
  EXPECT_TRUE(expected_packets_iterator_ != expectations_.packets_.end());
}

void FakeAudioRenderer::EndOfStream() { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::DiscardAllPackets(DiscardAllPacketsCallback callback) {
  callback();
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::DiscardAllPacketsNoReply() { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::Play(int64_t reference_time, int64_t media_time, PlayCallback callback) {
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::PlayNoReply(int64_t reference_time, int64_t media_time) {
  EXPECT_TRUE(expected_packets_iterator_ == expectations_.packets_.end());
  EXPECT_TRUE(send_packet_callback_);

  if (send_packet_callback_ && !expectations_.block_completion_) {
    send_packet_callback_();
  }

  play_no_reply_called_ = true;
}

void FakeAudioRenderer::Pause(PauseCallback callback) {}

void FakeAudioRenderer::PauseNoReply() {
  Pause([](int64_t reference_time, int64_t media_time) {});
}

void FakeAudioRenderer::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> request) {
  FX_NOTIMPLEMENTED();
}

void FakeAudioRenderer::EnableMinLeadTimeEvents(bool enabled) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::GetMinLeadTime(GetMinLeadTimeCallback callback) { FX_NOTIMPLEMENTED(); }

void FakeAudioRenderer::SetUsage(fuchsia::media::AudioRenderUsage usage) {
  EXPECT_EQ(expectations_.usage_, usage);

  set_usage_called_ = true;
}

}  // namespace test
}  // namespace soundplayer
