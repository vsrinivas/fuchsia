// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>

#include <memory>

#include "../codec_factory_app.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

class CodecFactoryHwDetectTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    codec_factory_ =
        std::make_unique<CodecFactoryApp>(dispatcher(), CodecFactoryApp::ProdOrTest::kTesting);
  }

  void TearDown() override { codec_factory_.reset(); }

  std::unique_ptr<CodecFactoryApp> codec_factory_;
};

TEST_F(CodecFactoryHwDetectTest, H264DecoderPresent) {
  // Loop needs to run till hw codec are fully discovered, so run test till that happens.
  RunLoopUntil([this]() {
    auto factory = codec_factory_->FindHwCodec(
        [](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
          std::string mime_type = "video/h264";
          return (fuchsia::mediacodec::CodecType::DECODER == hw_codec_description.codec_type) &&
                 (mime_type == hw_codec_description.mime_type);
        });
    return factory != nullptr;
  });
}
