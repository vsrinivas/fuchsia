// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/api/fuzzer/fuzzed_client.h"

namespace media::audio::test {

fit::function<bool()> FuzzedCapturer::Step() {
  if (async_capture_active_) {
    switch (data().ConsumeEnum<CaptureStep>()) {
      case CaptureStep::kOnPacketProduced:
        while (!captured_packets_.empty()) {
          client()->fidl()->ReleasePacket(captured_packets_.front());
          captured_packets_.pop();
        }
        break;
      case CaptureStep::kStopAsyncCapture:
        client()->fidl()->StopAsyncCapture([this]() { async_capture_active_ = false; });
        break;
      case CaptureStep::kStopAsyncCaptureNoReply:
        client()->fidl()->StopAsyncCaptureNoReply();
        // Because we cannot explicitly synchronize with this call to ensure we are no longer in
        // async mode, we end.
        set_end_of_stream(true);
        break;
      default:
        Random();
        break;
    }
  } else {
    async_capture_active_ = !sync_capture_active_ && data().ConsumeBool();
    if (async_capture_active_) {
      auto done = std::make_shared<bool>(false);
      client()->fidl().events().OnPacketProduced = [this, done](fuchsia::media::StreamPacket p) {
        captured_packets_.push(p);
        *done = true;
      };
      client()->fidl()->StartAsyncCapture(kPacketFrames);
      return [done]() { return *done; };
    } else {
      switch (data().ConsumeEnum<CaptureStep>()) {
        case CaptureStep::kCaptureAt:
          sync_capture_active_ = true;
          client()->fidl()->CaptureAt(0, 0, kPacketFrames, [this](fuchsia::media::StreamPacket) {
            sync_capture_active_ = false;
          });
          break;
        case CaptureStep::kDiscardAllPackets:
          client()->fidl()->DiscardAllPackets([this]() { set_end_of_stream(true); });
          break;
        case CaptureStep::kDiscardAllPacketsNoReply:
          client()->fidl()->DiscardAllPacketsNoReply();
          set_end_of_stream(true);
          break;
        default:
          Random();
          break;
      }
    }
  }
  return []() { return true; };
}

void FuzzedCapturer::Random() {
  switch (data().ConsumeIntegralInRange(0, 4)) {
    case 0:
      BindGainControl();
      break;
    case 1:
      client()->fidl()->GetReferenceClock([](zx::clock) {});
      break;
    case 2:
      client()->fidl()->GetStreamType([](fuchsia::media::StreamType) {});
      break;
    case 3:
      client()->fidl()->SetUsage(fuchsia::media::AudioCaptureUsage::BACKGROUND);
      break;
  }
}

}  // namespace media::audio::test
