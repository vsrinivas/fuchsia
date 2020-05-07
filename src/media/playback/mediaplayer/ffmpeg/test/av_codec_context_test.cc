// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/ffmpeg/av_codec_context.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/playback/mediaplayer/graph/types/audio_stream_type.h"
extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/encryption_info.h"
}

namespace media_player::test {

// Verifies that encryption parameters are properly generated from an |AVStream| for an
// encrypted stream.
TEST(AvCodecContext, EncryptionParameters) {
  constexpr uint32_t kChannels = 2;
  constexpr uint32_t kSampleRate = 48000;
  const std::vector<uint8_t> kSystemId = {0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
                                          0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0};
  const std::vector<std::vector<uint8_t>> kKeyIds = {
      {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
       0x0f},
      {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
       0xff}};
  const std::vector<uint8_t> kData = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  const std::vector<uint8_t> kEncryptionParametersGolden = {
      0x00, 0x00, 0x00, 0x54,  // uint32_t size;
      0x70, 0x73, 0x73, 0x68,  // uint32_t type; // fourcc 'pssh'
      0x01,                    // uint8_t version;
      0x00, 0x00, 0x00,        // uint8_t flags[3]; // all zeros
      0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
      0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,  // uint8_t system_id[16];
      0x00, 0x00, 0x00, 0x02,                          // uint32_t key_id_count;
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,  // uint8_t key_id[16];
      0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
      0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,  // uint8_t key_id[16];
      0x00, 0x00, 0x00, 0x10,                          // uint32_t data_size;
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};  // uint8_t data[data_size];

  // Build an |AVStream| structure.
  AVStream av_stream;
  AVCodecParameters av_codec_parameters;

  av_stream.codecpar = &av_codec_parameters;
  av_codec_parameters.codec_type = AVMEDIA_TYPE_AUDIO;
  av_codec_parameters.codec_id = AV_CODEC_ID_PCM_S16LE;
  av_codec_parameters.extradata_size = 0;
  av_codec_parameters.extradata = nullptr;
  av_codec_parameters.format = AV_SAMPLE_FMT_S16;
  av_codec_parameters.channels = kChannels;
  av_codec_parameters.sample_rate = kSampleRate;

  // Build an |AVEncryptionInitInfo| structure.
  AVEncryptionInitInfo* av_encryption_init_info = av_encryption_init_info_alloc(
      kSystemId.size(), kKeyIds.size(), kKeyIds[0].size(), kData.size());

  FX_CHECK(av_encryption_init_info);

  FX_CHECK(av_encryption_init_info->system_id);
  FX_CHECK(av_encryption_init_info->system_id_size == kSystemId.size());
  memcpy(av_encryption_init_info->system_id, kSystemId.data(), kSystemId.size());

  FX_CHECK(av_encryption_init_info->key_ids);
  FX_CHECK(av_encryption_init_info->num_key_ids == kKeyIds.size());
  FX_CHECK(av_encryption_init_info->key_id_size == kKeyIds[0].size());
  for (size_t i = 0; i < kKeyIds.size(); ++i) {
    FX_CHECK(av_encryption_init_info->key_ids[i]);
    EXPECT_EQ(kKeyIds[0].size(), kKeyIds[i].size());
    memcpy(av_encryption_init_info->key_ids[i], kKeyIds[i].data(), kKeyIds[i].size());
  }

  FX_CHECK(av_encryption_init_info->data);
  FX_CHECK(av_encryption_init_info->data_size == kData.size());
  memcpy(av_encryption_init_info->data, kData.data(), kData.size());

  FX_CHECK(av_encryption_init_info->next == 0);

  // Add the |AVEncryptionInitInfo| to the |AVStream| as side data.
  size_t side_data_size;
  auto side_data_bytes =
      av_encryption_init_info_add_side_data(av_encryption_init_info, &side_data_size);
  FX_CHECK(side_data_bytes);
  FX_CHECK(side_data_size > 0);

  av_encryption_init_info_free(av_encryption_init_info);

  AVPacketSideData av_packet_side_data;
  av_packet_side_data.type = AV_PKT_DATA_ENCRYPTION_INIT_INFO;
  av_packet_side_data.data = side_data_bytes;
  av_packet_side_data.size = static_cast<int>(side_data_size);

  av_stream.side_data = &av_packet_side_data;
  av_stream.nb_side_data = 1;

  // Create a |StreamType| from the |AVStream|.
  auto stream_type = AvCodecContext::GetStreamType(av_stream);
  EXPECT_NE(nullptr, stream_type);

  av_free(side_data_bytes);

  // Verify the |StreamType|.
  EXPECT_EQ(StreamType::Medium::kAudio, stream_type->medium());
  EXPECT_TRUE(stream_type->encrypted());
  EXPECT_NE(nullptr, stream_type->encryption_parameters());
  EXPECT_EQ(kEncryptionParametersGolden.size(), stream_type->encryption_parameters()->size());
  EXPECT_EQ(0,
            memcmp(kEncryptionParametersGolden.data(), stream_type->encryption_parameters()->data(),
                   stream_type->encryption_parameters()->size()));
  EXPECT_EQ(StreamType::kAudioEncodingLpcm, stream_type->encoding());
  EXPECT_EQ(nullptr, stream_type->encoding_parameters());

  auto audio_stream_type = stream_type->audio();
  EXPECT_NE(nullptr, audio_stream_type);
  EXPECT_EQ(kChannels, audio_stream_type->channels());
  EXPECT_EQ(kSampleRate, audio_stream_type->frames_per_second());
}

}  // namespace media_player::test
