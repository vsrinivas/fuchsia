#include <lib/async-loop/default.h>
#include <lib/gtest/real_loop_fixture.h>

#include "../codec_factory_app.h"

using CodecFactoryHwDetectTest = ::gtest::RealLoopFixture;

TEST_F(CodecFactoryHwDetectTest, H264DecoderPresent) {
  codec_factory::CodecFactoryApp app(dispatcher());

  RunLoopUntil([&app]() {
    auto factory = app.FindHwCodec(
        [](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
          std::string mime_type = "video/h264";
          return (fuchsia::mediacodec::CodecType::DECODER == hw_codec_description.codec_type) &&
                 (mime_type == hw_codec_description.mime_type);
        });
    ;
    return factory != nullptr;
  });
}

TEST_F(CodecFactoryHwDetectTest, H264EnoderNotPresent) {
  codec_factory::CodecFactoryApp app(dispatcher());

  RunLoopUntilIdle();

  auto factory = app.FindHwCodec(
      [](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
        std::string mime_type = "video/h264";
        return (fuchsia::mediacodec::CodecType::ENCODER == hw_codec_description.codec_type) &&
               (mime_type == hw_codec_description.mime_type);
      });

  ASSERT_EQ(factory, nullptr);
}
