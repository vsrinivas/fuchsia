// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include "../codec_factory_app.h"

using CodecFactoryHwDetectTest = ::gtest::RealLoopFixture;

TEST_F(CodecFactoryHwDetectTest, H264EncoderPresent) {
  codec_factory::CodecFactoryApp app(dispatcher());

  // Loop needs to run till hw codec are fully discovered, so run test till that happens.
  RunLoopUntil([&app]() {
    auto factory = app.FindHwCodec(
        [](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
          std::string mime_type = "video/h264";
          return (fuchsia::mediacodec::CodecType::ENCODER == hw_codec_description.codec_type) &&
                 (mime_type == hw_codec_description.mime_type);
        });
    return factory != nullptr;
  });
}

TEST_F(CodecFactoryHwDetectTest, H265EncoderPresent) {
  codec_factory::CodecFactoryApp app(dispatcher());

  // Loop needs to run till hw codec are fully discovered, so run test till that happens.
  RunLoopUntil([&app]() {
    auto factory = app.FindHwCodec(
        [](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
          std::string mime_type = "video/h265";
          return (fuchsia::mediacodec::CodecType::ENCODER == hw_codec_description.codec_type) &&
                 (mime_type == hw_codec_description.mime_type);
        });
    return factory != nullptr;
  });
}
