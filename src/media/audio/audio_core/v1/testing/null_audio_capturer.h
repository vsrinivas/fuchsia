// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_NULL_AUDIO_CAPTURER_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_NULL_AUDIO_CAPTURER_H_

#include <fuchsia/media/cpp/fidl.h>

namespace media::audio::test {

// A |fuchsia::media::AudioCapturer| that simply does nothing.
class NullAudioCapturer : public fuchsia::media::AudioCapturer {
 private:
  void AddPayloadBuffer(uint32_t id, ::zx::vmo payload_buffer) override {}
  void RemovePayloadBuffer(uint32_t id) override {}
  void ReleasePacket(fuchsia::media::StreamPacket packet) override {}
  void DiscardAllPackets(DiscardAllPacketsCallback callback) override {}
  void DiscardAllPacketsNoReply() override {}
  void SetPcmStreamType(fuchsia::media::AudioStreamType stream_type) override {}
  void CaptureAt(uint32_t payload_buffer_id, uint32_t payload_offset, uint32_t frames,
                 CaptureAtCallback callback) override {}
  void StartAsyncCapture(uint32_t frames_per_packet) override {}
  void StopAsyncCapture(StopAsyncCaptureCallback callback) override {}
  void StopAsyncCaptureNoReply() override {}
  void BindGainControl(::fidl::InterfaceRequest<::fuchsia::media::audio::GainControl>
                           gain_control_request) override {}
  void GetReferenceClock(GetReferenceClockCallback callback) override {}
  void SetReferenceClock(::zx::clock ref_clock) override {}
  void SetUsage(fuchsia::media::AudioCaptureUsage usage) override {}
  void GetStreamType(GetStreamTypeCallback callback) override {}
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_NULL_AUDIO_CAPTURER_H_
