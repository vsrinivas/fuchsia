// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/player/test/fake_video_renderer.h"
#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"

namespace media_player {
namespace test {

// static
std::shared_ptr<FakeVideoRenderer> FakeVideoRenderer::Create() {
  return std::make_shared<FakeVideoRenderer>();
}

FakeVideoRenderer::FakeVideoRenderer() {
  supported_stream_types_.push_back(VideoStreamTypeSet::Create(
      {StreamType::kVideoEncodingUncompressed}, Range<uint32_t>(1, 3840),
      Range<uint32_t>(1, 2160)));
}

}  // namespace test
}  // namespace media_player
