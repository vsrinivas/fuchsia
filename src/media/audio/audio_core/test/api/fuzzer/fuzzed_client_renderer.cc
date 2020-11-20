// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/api/fuzzer/fuzzed_client.h"

namespace media::audio::test {

fit::function<bool()> FuzzedRenderer::Step() {
  switch (data().ConsumeEnum<RenderStep>()) {
    case RenderStep::kSendPacket:
      if (sent_packets_ < kMaxPackets) {
        auto done = std::make_shared<bool>(false);
        sent_packets_++;
        client()->fidl()->SendPacket(fidl::Clone(packet_), [done]() { *done = true; });
        return [done]() { return *done; };
      }
      break;
    case RenderStep::kSendPacketNoReply:
      if (sent_packets_ < kMaxPackets) {
        sent_packets_++;
        client()->fidl()->SendPacketNoReply(fidl::Clone(packet_));
      }
      break;
    case RenderStep::kPause:
      client()->fidl()->Pause([](int64_t, int64_t) {});
      data().ConsumeBool()
          ? client()->fidl()->Play(fuchsia::media::NO_TIMESTAMP, fuchsia::media::NO_TIMESTAMP,
                                   [](int64_t, int64_t) {})
          : client()->fidl()->PlayNoReply(fuchsia::media::NO_TIMESTAMP,
                                          fuchsia::media::NO_TIMESTAMP);
      break;
    case RenderStep::kPauseNoReply:
      client()->fidl()->PauseNoReply();
      break;
    case RenderStep::kDiscardAllPackets:
      client()->fidl()->DiscardAllPackets([this]() { set_end_of_stream(true); });
      break;
    case RenderStep::kDiscardAllPacketsNoReply:
      client()->fidl()->DiscardAllPacketsNoReply();
      set_end_of_stream(true);
      break;
    default:
      Random();
      break;
  }

  if (Done()) {
    client()->fidl()->EndOfStream();
  }
  return []() { return true; };
}

void FuzzedRenderer::Random() {
  switch (data().ConsumeIntegralInRange(0, 4)) {
    case 0:
      BindGainControl();
      break;
    case 1:
      client()->fidl()->EnableMinLeadTimeEvents(data().ConsumeBool());
      break;
    case 2:
      client()->fidl()->GetMinLeadTime([](long) {});
      break;
    case 3:
      client()->fidl()->GetReferenceClock([](zx::clock) {});
      break;
  }
}

}  // namespace media::audio::test
