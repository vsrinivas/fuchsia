// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/vnext/lib/ffmpeg/av_codec_context.h"

namespace fuchsia::math {

bool operator==(const Size& a, const Size& b) { return a.width == b.width && a.height == b.height; }

}  // namespace fuchsia::math

namespace fmlib {
namespace {

constexpr uint32_t kChannelCount = 3;
constexpr uint32_t kFramesPerSecond = 48000;
const fidl::VectorPtr<uint8_t> kCompressionParameters(std::vector<uint8_t>{1, 2, 3, 4});
const fuchsia::math::Size kCodedSize{.width = 1024, .height = 768};
const fuchsia::math::Size kH264CodedSize{.width = 1024, .height = 770};
const fuchsia::math::Size kDisplaySize{.width = 1024, .height = 720};
const fuchsia::math::Size kAspectRatio{.width = 1, .height = 1};
const AVCodec* kPlaceholderCodec = reinterpret_cast<AVCodec*>(1);  // Any non-null value is fine.

bool VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat sample_format,
                             std::unique_ptr<Compression> compression, AVCodecID codec_id,
                             AVSampleFormat av_sample_format) {
  auto under_test = AvCodecContext::Create(
      AudioFormat(sample_format, kChannelCount, kFramesPerSecond, std::move(compression)));

  // We EXPECT so we can see what aspect is unexpected. The caller should EXPECT_TRUE the result of
  // calling this function in order to identify the offending parameters.
  EXPECT_TRUE(!!under_test);
  if (!under_test) {
    return false;
  }

  EXPECT_EQ(AVMEDIA_TYPE_AUDIO, under_test->codec_type);
  EXPECT_EQ(codec_id, under_test->codec_id);
  EXPECT_EQ(av_sample_format, under_test->sample_fmt);
  EXPECT_EQ(kChannelCount, static_cast<uint32_t>(under_test->channels));
  EXPECT_EQ(kFramesPerSecond, static_cast<uint32_t>(under_test->sample_rate));

  return (AVMEDIA_TYPE_AUDIO == under_test->codec_type) && (codec_id == under_test->codec_id) &&
         (av_sample_format == under_test->sample_fmt) &&
         (kChannelCount == static_cast<uint32_t>(under_test->channels)) &&
         (kFramesPerSecond == static_cast<uint32_t>(under_test->sample_rate));
}

bool VerifyVideoCodecContext(std::unique_ptr<Compression> compression, AVCodecID codec_id) {
  auto under_test = AvCodecContext::Create(
      VideoFormat(fuchsia::mediastreams::PixelFormat::I420,
                  fuchsia::mediastreams::ColorSpace::REC709, kCodedSize, kDisplaySize,
                  std::make_unique<fuchsia::math::Size>(kAspectRatio), std::move(compression)));

  // We EXPECT so we can see what aspect is unexpected. The caller should EXPECT_TRUE the result of
  // calling this function in order to identify the offending parameters.
  EXPECT_TRUE(!!under_test);
  if (!under_test) {
    return false;
  }

  EXPECT_EQ(AVMEDIA_TYPE_VIDEO, under_test->codec_type);
  EXPECT_EQ(codec_id, under_test->codec_id);
  EXPECT_EQ(AV_PIX_FMT_YUV420P, under_test->pix_fmt);
  EXPECT_EQ(kCodedSize.width, static_cast<int32_t>(under_test->coded_width));
  EXPECT_EQ(kCodedSize.height, static_cast<int32_t>(under_test->coded_height));
  EXPECT_EQ(kAspectRatio.width, static_cast<int32_t>(under_test->sample_aspect_ratio.num));
  EXPECT_EQ(kAspectRatio.height, static_cast<int32_t>(under_test->sample_aspect_ratio.den));

  return (AVMEDIA_TYPE_VIDEO == under_test->codec_type) && (codec_id == under_test->codec_id) &&
         (AV_PIX_FMT_YUV420P == under_test->pix_fmt) &&
         (kCodedSize.width == static_cast<int32_t>(under_test->coded_width)) &&
         (kCodedSize.height == static_cast<int32_t>(under_test->coded_height)) &&
         (kAspectRatio.width == static_cast<int32_t>(under_test->sample_aspect_ratio.num)) &&
         (kAspectRatio.height == static_cast<int32_t>(under_test->sample_aspect_ratio.den));
}

bool VerifyMediaFormat(const MediaFormat& format,
                       fuchsia::mediastreams::AudioSampleFormat sample_format, uint32_t channels,
                       uint32_t sample_rate, const char* compression_type = nullptr) {
  EXPECT_TRUE(format.is_audio());
  if (!format.is_audio()) {
    return false;
  }

  EXPECT_EQ(sample_format, format.audio().sample_format());
  EXPECT_EQ(channels, format.audio().channel_count());
  EXPECT_EQ(sample_rate, format.audio().frames_per_second());
  if (compression_type) {
    EXPECT_TRUE(format.is_compressed());
    if (format.is_compressed()) {
      EXPECT_EQ(compression_type, format.compression().type());
    }
  } else {
    EXPECT_FALSE(format.is_compressed());
  }

  return (sample_format == format.audio().sample_format() &&
          channels == format.audio().channel_count() &&
          sample_rate == format.audio().frames_per_second() &&
          ((compression_type && format.is_compressed() &&
            std::string(compression_type) == format.compression().type()) ||
           (!compression_type && !format.is_compressed())));
}

bool VerifyMediaFormat(const MediaFormat& format, fuchsia::math::Size coded_size,
                       fuchsia::math::Size display_size, fuchsia::math::Size aspect_ratio,
                       fuchsia::mediastreams::PixelFormat pixel_format,
                       fuchsia::mediastreams::ColorSpace color_space,
                       const char* compression_type = nullptr) {
  EXPECT_TRUE(format.is_video());
  if (!format.is_video()) {
    return false;
  }

  EXPECT_EQ(coded_size, format.video().coded_size());
  EXPECT_EQ(display_size, format.video().display_size());
  EXPECT_TRUE(!!format.video().aspect_ratio());
  EXPECT_EQ(aspect_ratio, *format.video().aspect_ratio());
  EXPECT_EQ(pixel_format, format.video().pixel_format());
  EXPECT_EQ(color_space, format.video().color_space());
  if (compression_type) {
    EXPECT_TRUE(format.is_compressed());
    if (format.is_compressed()) {
      EXPECT_EQ(compression_type, format.compression().type());
    }
  } else {
    EXPECT_FALSE(format.is_compressed());
  }

  return (coded_size == format.video().coded_size() &&
          display_size == format.video().display_size() &&
          aspect_ratio == *format.video().aspect_ratio() &&
          color_space == format.video().color_space() &&
          ((compression_type && format.is_compressed() &&
            std::string(compression_type) == format.compression().type()) ||
           (!compression_type && !format.is_compressed())));
}

bool VerifyStringArray(const std::vector<std::string>& expected,
                       const std::vector<std::string>& actual) {
  EXPECT_EQ(expected.size(), actual.size());
  bool result = expected.size() == actual.size();

  for (auto& s : expected) {
    auto count = std::count(actual.begin(), actual.end(), s);
    EXPECT_EQ(1u, count) << "Count of " << s << " in vector is " << count << ", expected 1.";
    if (count != 1) {
      result = false;
    }
  }

  return result;
}

// Tests the |Create| method for audio formats.
TEST(AvCodecContextTest, CreateAudio) {
  // Test PCM formats.
  EXPECT_TRUE(VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, nullptr,
                                      AV_CODEC_ID_PCM_U8, AV_SAMPLE_FMT_U8));
  EXPECT_TRUE(VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, nullptr,
                                      AV_CODEC_ID_PCM_S16LE, AV_SAMPLE_FMT_S16));
  EXPECT_TRUE(VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32,
                                      nullptr, AV_CODEC_ID_PCM_S24LE, AV_SAMPLE_FMT_S32));
  EXPECT_TRUE(VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, nullptr,
                                      AV_CODEC_ID_PCM_S32LE, AV_SAMPLE_FMT_S32));
  EXPECT_TRUE(VerifyAudioCodecContext(fuchsia::mediastreams::AudioSampleFormat::FLOAT, nullptr,
                                      AV_CODEC_ID_PCM_F32LE, AV_SAMPLE_FMT_FLT));

  // Test SBC formats. |AudioSampleFormat| is significant for SBC.
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC, nullptr),
      AV_CODEC_ID_SBC, AV_SAMPLE_FMT_U8P));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_16,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC, nullptr),
      AV_CODEC_ID_SBC, AV_SAMPLE_FMT_S16P));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC, nullptr),
      AV_CODEC_ID_SBC, AV_SAMPLE_FMT_S32P));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_32,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC, nullptr),
      AV_CODEC_ID_SBC, AV_SAMPLE_FMT_S32P));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC, nullptr),
      AV_CODEC_ID_SBC, AV_SAMPLE_FMT_FLTP));

  // Test compressed formats. |AudioSampleFormat| value has no effect here.
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_AAC, nullptr),
      AV_CODEC_ID_AAC, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM, nullptr),
      AV_CODEC_ID_AAC_LATM, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB, nullptr),
      AV_CODEC_ID_AMR_NB, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB, nullptr),
      AV_CODEC_ID_AMR_WB, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_APTX, nullptr),
      AV_CODEC_ID_APTX, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC, nullptr),
      AV_CODEC_ID_FLAC, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS, nullptr),
      AV_CODEC_ID_GSM_MS, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_MP3, nullptr),
      AV_CODEC_ID_MP3, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS, nullptr),
      AV_CODEC_ID_OPUS, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW, nullptr),
      AV_CODEC_ID_PCM_ALAW, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW, nullptr),
      AV_CODEC_ID_PCM_MULAW, AV_SAMPLE_FMT_NONE));
  EXPECT_TRUE(VerifyAudioCodecContext(
      fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS, nullptr),
      AV_CODEC_ID_VORBIS, AV_SAMPLE_FMT_NONE));

  // Expect no support for an unsupported compression type.
  EXPECT_FALSE(!!AvCodecContext::Create(AudioFormat(
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount, kFramesPerSecond,
      std::make_unique<Compression>("acme_squeeze", nullptr), nullptr)));

  // Expect no support for an encrypted stream.
  EXPECT_FALSE(!!AvCodecContext::Create(AudioFormat(
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount, kFramesPerSecond,
      /* compression */ nullptr,
      std::make_unique<Encryption>("scheme", nullptr, nullptr, nullptr))));

  // Expect that compression parameters are copied to the context's 'extradata'.
  auto under_test = AvCodecContext::Create(AudioFormat(
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount, kFramesPerSecond,
      std::make_unique<Compression>(fuchsia::mediastreams::AUDIO_COMPRESSION_SBC,
                                    kCompressionParameters),
      /* encryption */ nullptr));
  EXPECT_TRUE(!!under_test);
  EXPECT_TRUE(!!under_test->extradata);
  EXPECT_EQ(kCompressionParameters->size(), static_cast<size_t>(under_test->extradata_size));
  EXPECT_EQ(*kCompressionParameters,
            std::vector<uint8_t>(under_test->extradata,
                                 under_test->extradata + under_test->extradata_size));
}

// Tests the |Create| method for video formats.
TEST(AvCodecContextTest, CreateVideo) {
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_H263, nullptr),
      AV_CODEC_ID_H263));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_H264, nullptr),
      AV_CODEC_ID_H264));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4, nullptr),
      AV_CODEC_ID_MPEG4));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA, nullptr),
      AV_CODEC_ID_THEORA));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_VP3, nullptr),
      AV_CODEC_ID_VP3));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_VP8, nullptr),
      AV_CODEC_ID_VP8));
  EXPECT_TRUE(VerifyVideoCodecContext(
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_VP9, nullptr),
      AV_CODEC_ID_VP9));

  // Expect no support for an unsupported compression type.
  EXPECT_FALSE(!!AvCodecContext::Create(VideoFormat(
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      kCodedSize, kDisplaySize, std::make_unique<fuchsia::math::Size>(kAspectRatio),
      std::make_unique<Compression>("acme_squeeze", nullptr), /* encryption */ nullptr)));

  // Expect no support for uncompressed video.
  EXPECT_FALSE(!!AvCodecContext::Create(VideoFormat(
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      kCodedSize, kDisplaySize, std::make_unique<fuchsia::math::Size>(kAspectRatio),
      /* compression */ nullptr, /* encryption */ nullptr)));

  // Expect no support for an encrypted stream.
  EXPECT_FALSE(!!AvCodecContext::Create(VideoFormat(
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      kCodedSize, kDisplaySize, std::make_unique<fuchsia::math::Size>(kAspectRatio),
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_H263, nullptr),
      std::make_unique<Encryption>("scheme", nullptr, nullptr, nullptr))));

  // Expect that compression parameters are copied to the context's 'extradata'.
  auto under_test = AvCodecContext::Create(VideoFormat(
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      kCodedSize, kDisplaySize, std::make_unique<fuchsia::math::Size>(kAspectRatio),
      std::make_unique<Compression>(fuchsia::mediastreams::VIDEO_COMPRESSION_H263,
                                    kCompressionParameters),
      /* encryption */ nullptr));
  EXPECT_TRUE(!!under_test);
  EXPECT_TRUE(!!under_test->extradata);
  EXPECT_EQ(kCompressionParameters->size(), static_cast<size_t>(under_test->extradata_size));
  EXPECT_EQ(*kCompressionParameters,
            std::vector<uint8_t>(under_test->extradata,
                                 under_test->extradata + under_test->extradata_size));
}

// Tests the |GetMediaFormat| method that accepts |AVCodecContext|.
TEST(AvCodecContextTest, GetMediaFormatFromContext) {
  // For audio, test all the sample formats.
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_U8,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_U8}),
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_U8,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_U8P}),
      fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S16LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S16}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S16LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S16P}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S24LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S32}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S24LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S32P}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S32LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S32}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_S32LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_S32P}),
      fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_F32LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLT}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_F32LE,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond));

  // For audio, test all the compression formats.
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_AAC,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_AAC));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_AAC_LATM,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_AMR_NB,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_AMR_WB,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_APTX,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_APTX));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_FLAC,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_GSM_MS,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_MP3,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_MP3));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_OPUS,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_ALAW,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_PCM_MULAW,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_SBC,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_SBC));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                                                    .codec = nullptr,
                                                    .codec_id = AV_CODEC_ID_VORBIS,
                                                    .sample_rate = kFramesPerSecond,
                                                    .channels = kChannelCount,
                                                    .sample_fmt = AV_SAMPLE_FMT_FLTP}),
      fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount, kFramesPerSecond,
      fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS));

  // For audio, ensure that compression parameters are copied correctly.
  auto media_format = AvCodecContext::GetMediaFormat(
      AVCodecContext{.codec_type = AVMEDIA_TYPE_AUDIO,
                     .codec = nullptr,
                     .codec_id = AV_CODEC_ID_VORBIS,
                     .extradata = const_cast<uint8_t*>(kCompressionParameters->data()),
                     .extradata_size = static_cast<int>(kCompressionParameters->size()),
                     .sample_rate = kFramesPerSecond,
                     .channels = kChannelCount,
                     .sample_fmt = AV_SAMPLE_FMT_FLTP});
  EXPECT_TRUE(media_format.is_audio());
  EXPECT_TRUE(media_format.audio().is_compressed());
  EXPECT_TRUE(media_format.audio().compression().parameters().has_value());
  EXPECT_EQ(*kCompressionParameters, media_format.audio().compression().parameters());

  // For video, test pixel formats and color spaces for uncompressed formats. A context with a
  // codec is assumed to describe the output of the codec, meaning the format is uncompressed.
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = kPlaceholderCodec,
          .codec_id = AV_CODEC_ID_H263,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = kPlaceholderCodec,
          .codec_id = AV_CODEC_ID_H263,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUVJ420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_BT709,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = kPlaceholderCodec,
          .codec_id = AV_CODEC_ID_H263,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUVJ420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_SMPTE170M,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC601_NTSC));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = kPlaceholderCodec,
          .codec_id = AV_CODEC_ID_H263,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUVJ420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_BT470BG,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC601_NTSC));

  // For video, test compressed formats.
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_H263,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_H263));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_H264,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kH264CodedSize,  // H264 decoder uses a different alignment than the other decoders.
      kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_H264));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_MPEG4,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_THEORA,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_VP3,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_VP3));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_VP8,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_VP8));
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(AVCodecContext{
          .codec_type = AVMEDIA_TYPE_VIDEO,
          .codec = nullptr,
          .codec_id = AV_CODEC_ID_VP9,
          .width = kDisplaySize.width,
          .height = kDisplaySize.height,
          .coded_width = kCodedSize.width,
          .coded_height = kCodedSize.height,
          .pix_fmt = AV_PIX_FMT_YUV420P,
          .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
          .colorspace = AVCOL_SPC_UNSPECIFIED,
          .color_range = AVCOL_RANGE_MPEG}),
      kCodedSize, kDisplaySize, kAspectRatio, fuchsia::mediastreams::PixelFormat::I420,
      fuchsia::mediastreams::ColorSpace::REC709, fuchsia::mediastreams::VIDEO_COMPRESSION_VP9));

  // For video, ensure that compression parameters are copied correctly.
  media_format = AvCodecContext::GetMediaFormat(
      AVCodecContext{.codec_type = AVMEDIA_TYPE_VIDEO,
                     .codec = nullptr,
                     .codec_id = AV_CODEC_ID_VP9,
                     .extradata = const_cast<uint8_t*>(kCompressionParameters->data()),
                     .extradata_size = static_cast<int>(kCompressionParameters->size()),
                     .width = kDisplaySize.width,
                     .height = kDisplaySize.height,
                     .coded_width = kCodedSize.width,
                     .coded_height = kCodedSize.height,
                     .pix_fmt = AV_PIX_FMT_YUV420P,
                     .sample_aspect_ratio{.num = kAspectRatio.width, .den = kAspectRatio.height},
                     .colorspace = AVCOL_SPC_UNSPECIFIED,
                     .color_range = AVCOL_RANGE_MPEG});
  EXPECT_TRUE(media_format.is_video());
  EXPECT_TRUE(media_format.video().is_compressed());
  EXPECT_TRUE(media_format.video().compression().parameters().has_value());
  EXPECT_EQ(*kCompressionParameters, media_format.video().compression().parameters());
}

// Tests the |GetMediaFormat| method that accepts |AVStream|.
TEST(AvCodecContextTest, GetMediaFormatFromStream) {
  // Initialize |codec_parameters| for audio.
  AVCodecParameters codec_parameters{
      .codec_type = AVMEDIA_TYPE_AUDIO, .channels = kChannelCount, .sample_rate = kFramesPerSecond};
  AVStream av_stream{.nb_side_data = 0, .codecpar = &codec_parameters};

  // For audio, test all the uncompressed sample formats.
  codec_parameters.codec_id = AV_CODEC_ID_PCM_U8;
  codec_parameters.format = AV_SAMPLE_FMT_U8;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_U8;
  codec_parameters.format = AV_SAMPLE_FMT_U8P;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S16LE;
  codec_parameters.format = AV_SAMPLE_FMT_S16;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S16LE;
  codec_parameters.format = AV_SAMPLE_FMT_S16P;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S24LE;
  codec_parameters.format = AV_SAMPLE_FMT_S32;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32,
                                kChannelCount, kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S24LE;
  codec_parameters.format = AV_SAMPLE_FMT_S32P;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32,
                                kChannelCount, kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S32LE;
  codec_parameters.format = AV_SAMPLE_FMT_S32;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_S32LE;
  codec_parameters.format = AV_SAMPLE_FMT_S32P;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_F32LE;
  codec_parameters.format = AV_SAMPLE_FMT_FLT;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_F32LE;
  codec_parameters.format = AV_SAMPLE_FMT_FLTP;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond));

  // For audio, test all the compressed formats.
  codec_parameters.codec_id = AV_CODEC_ID_AAC;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_AAC));
  codec_parameters.codec_id = AV_CODEC_ID_AAC_LATM;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      kChannelCount, kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM));
  codec_parameters.codec_id = AV_CODEC_ID_AMR_NB;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB));
  codec_parameters.codec_id = AV_CODEC_ID_AMR_WB;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB));
  codec_parameters.codec_id = AV_CODEC_ID_APTX;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_APTX));
  codec_parameters.codec_id = AV_CODEC_ID_FLAC;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC));
  codec_parameters.codec_id = AV_CODEC_ID_GSM_MS;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS));
  codec_parameters.codec_id = AV_CODEC_ID_MP3;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_MP3));
  codec_parameters.codec_id = AV_CODEC_ID_OPUS;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_ALAW;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      kChannelCount, kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW));
  codec_parameters.codec_id = AV_CODEC_ID_PCM_MULAW;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), fuchsia::mediastreams::AudioSampleFormat::FLOAT,
      kChannelCount, kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW));
  codec_parameters.codec_id = AV_CODEC_ID_SBC;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_SBC));
  codec_parameters.codec_id = AV_CODEC_ID_VORBIS;
  EXPECT_TRUE(VerifyMediaFormat(AvCodecContext::GetMediaFormat(av_stream),
                                fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                                kFramesPerSecond, fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS));

  // For audio, ensure that compression parameters are copied correctly.
  codec_parameters.extradata = const_cast<uint8_t*>(kCompressionParameters->data());
  codec_parameters.extradata_size = static_cast<int>(kCompressionParameters->size());
  auto media_format = AvCodecContext::GetMediaFormat(av_stream);
  EXPECT_TRUE(media_format.is_audio());
  EXPECT_TRUE(media_format.audio().is_compressed());
  EXPECT_TRUE(media_format.audio().compression().parameters().has_value());
  EXPECT_EQ(*kCompressionParameters, media_format.audio().compression().parameters());

  // Initialize |codec_parameters| for video.
  codec_parameters.codec_type = AVMEDIA_TYPE_VIDEO;
  codec_parameters.width = kDisplaySize.width;
  codec_parameters.height = kDisplaySize.height;
  codec_parameters.sample_aspect_ratio.num = kAspectRatio.width;
  codec_parameters.sample_aspect_ratio.den = kAspectRatio.height;
  codec_parameters.color_range = AVCOL_RANGE_MPEG;
  codec_parameters.extradata = nullptr;
  codec_parameters.extradata_size = 0;

  // For video, test pixel formats and color spaces.
  codec_parameters.codec_id = AV_CODEC_ID_H263;
  codec_parameters.format = AV_PIX_FMT_YUV420P;
  codec_parameters.color_space = AVCOL_SPC_UNSPECIFIED;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_H263));
  codec_parameters.format = AV_PIX_FMT_YUVJ420P;
  codec_parameters.color_space = AVCOL_SPC_BT709;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_H263));
  codec_parameters.color_space = AVCOL_SPC_SMPTE170M;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC601_NTSC,
      fuchsia::mediastreams::VIDEO_COMPRESSION_H263));
  codec_parameters.color_space = AVCOL_SPC_BT470BG;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC601_NTSC,
      fuchsia::mediastreams::VIDEO_COMPRESSION_H263));

  // For video, test compression types.
  codec_parameters.codec_id = AV_CODEC_ID_H264;
  codec_parameters.format = AV_PIX_FMT_YUV420P;
  codec_parameters.color_space = AVCOL_SPC_UNSPECIFIED;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_H264));
  codec_parameters.codec_id = AV_CODEC_ID_MPEG4;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4));
  codec_parameters.codec_id = AV_CODEC_ID_THEORA;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA));
  codec_parameters.codec_id = AV_CODEC_ID_VP3;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_VP3));
  codec_parameters.codec_id = AV_CODEC_ID_VP8;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_VP8));
  codec_parameters.codec_id = AV_CODEC_ID_VP9;
  EXPECT_TRUE(VerifyMediaFormat(
      AvCodecContext::GetMediaFormat(av_stream), kDisplaySize, kDisplaySize, kAspectRatio,
      fuchsia::mediastreams::PixelFormat::I420, fuchsia::mediastreams::ColorSpace::REC709,
      fuchsia::mediastreams::VIDEO_COMPRESSION_VP9));

  // For video, ensure that compression parameters are copied correctly.
  codec_parameters.extradata = const_cast<uint8_t*>(kCompressionParameters->data());
  codec_parameters.extradata_size = static_cast<int>(kCompressionParameters->size());
  media_format = AvCodecContext::GetMediaFormat(av_stream);
  EXPECT_TRUE(media_format.is_video());
  EXPECT_TRUE(media_format.video().is_compressed());
  EXPECT_TRUE(media_format.video().compression().parameters().has_value());
  EXPECT_EQ(*kCompressionParameters, media_format.video().compression().parameters());
}

// Tests the |GetAudioDecoderCompressionTypes| method.
TEST(AvCodecContextTest, GetAudioDecoderCompressionTypes) {
  EXPECT_TRUE(VerifyStringArray(
      std::vector<std::string>{
          fuchsia::mediastreams::AUDIO_COMPRESSION_AAC,
          fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM,
          fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB,
          fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB,
          fuchsia::mediastreams::AUDIO_COMPRESSION_APTX,
          fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC,
          fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS,
          fuchsia::mediastreams::AUDIO_COMPRESSION_MP3,
          fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS,
          fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW,
          fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW,
          fuchsia::mediastreams::AUDIO_COMPRESSION_SBC,
          fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS,
      },
      AvCodecContext::GetAudioDecoderCompressionTypes()));
}

// Tests the |GetVideoDecoderCompressionTypes| method.
TEST(AvCodecContextTest, GetVideoDecoderCompressionTypes) {
  EXPECT_TRUE(VerifyStringArray(
      std::vector<std::string>{
          fuchsia::mediastreams::VIDEO_COMPRESSION_H263,
          fuchsia::mediastreams::VIDEO_COMPRESSION_H264,
          fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4,
          fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA,
          fuchsia::mediastreams::VIDEO_COMPRESSION_VP3,
          fuchsia::mediastreams::VIDEO_COMPRESSION_VP8,
          fuchsia::mediastreams::VIDEO_COMPRESSION_VP9,
      },
      AvCodecContext::GetVideoDecoderCompressionTypes()));
}

// Tests the |GetAudioEncoderCompressionTypes| method.
TEST(AvCodecContextTest, GetAudioEncoderCompressionTypes) {
  EXPECT_TRUE(VerifyStringArray(std::vector<std::string>{},
                                AvCodecContext::GetAudioEncoderCompressionTypes()));
}

// Tests the |GetVideoEncoderCompressionTypes| method.
TEST(AvCodecContextTest, GetVideoEncoderCompressionTypes) {
  EXPECT_TRUE(VerifyStringArray(std::vector<std::string>{},
                                AvCodecContext::GetVideoEncoderCompressionTypes()));
}

}  // namespace
}  // namespace fmlib
