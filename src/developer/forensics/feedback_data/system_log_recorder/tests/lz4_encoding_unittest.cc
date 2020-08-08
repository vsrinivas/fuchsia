// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_utils.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

const std::string kDecodingErrorStr = "!!! DECODING ERROR !!!\n";
const std::regex kDecodingSizeError(
    "(.*)(!!! CANNOT DECODE)(.*)(THERE ARE ONLY)(.*)(BYTES LEFT !!!\n)");

class Lz4ChunkDecoder : public Lz4Decoder {
 public:
  // Change visibility for testing.
  using Lz4Decoder::DecodeWithoutReset;
};

auto MakeEncodeDecodeChunk = [](Lz4Encoder* encoder, Lz4ChunkDecoder* decoder) {
  return [encoder, decoder](const std::string& input) -> std::string {
    auto chunk = encoder->Encode(input);
    return decoder->DecodeWithoutReset(chunk);
  };
};

TEST(EncodingSizeTest, TestEncodeDecodeSize) {
  for (uint16_t size = 0; size < 0xFFFF; size++) {
    std::string encoded = EncodeSize(size);
    const char* encoded_ptr = encoded.c_str();
    uint16_t size_rec = DecodeSize(&encoded_ptr);

    ASSERT_EQ(size, size_rec);
  }
}

TEST(EncodingTest, TestEncodeDecode_IncompleteData_NoContent) {
  // Choose encoder and decoder.
  auto encoder = Lz4Encoder();
  auto decoder = Lz4Decoder();

  // Setup encoded data.
  const std::string str_orig = "[0.0] Fuchsia lz4 encoding test log line 1\n";
  const std::string encoded_full = encoder.Encode(str_orig);

  const std::string encoded(encoded_full.begin(), encoded_full.begin() + 2);
  const std::string decoded = decoder.Decode(encoded);

  bool match = std::regex_search(decoded.cbegin(), decoded.cend(), kDecodingSizeError);
  EXPECT_TRUE(match);
}

TEST(EncodingTest, TestEncodeDecode_IncompleteData_MissingData) {
  // Choose encoder and decoder.
  auto encoder = Lz4Encoder();
  auto decoder = Lz4Decoder();

  // Setup encoded data.
  const std::string str_orig = "[0.0] Fuchsia lz4 encoding test log line 1\n";
  const std::string encoded_full = encoder.Encode(str_orig);

  const std::string encoded(encoded_full.begin(), encoded_full.end() - 1);
  const std::string decoded = decoder.Decode(encoded);

  bool match = std::regex_search(decoded.cbegin(), decoded.cend(), kDecodingSizeError);
  EXPECT_TRUE(match);
}

TEST(EncodingTest, TestDecodeInvalidData) {
  // Test the lz4 decoder by passing it an invalid encoded chunk.
  auto decoder = Lz4ChunkDecoder();

  const uint16_t encoded_size = 10;
  const std::string encoded_data = EncodeSize(encoded_size) + std::string(encoded_size, '\0');
  const std::string decoded_data = decoder.DecodeWithoutReset(encoded_data);

  EXPECT_EQ(decoded_data, kDecodingErrorStr);
}

TEST(EncodingTest, TestEncodeDecodeChunk) {
  // Choose encoder and decoder.
  auto encoder = Lz4Encoder();
  auto decoder = Lz4ChunkDecoder();
  auto EncodeDecodeChunk = MakeEncodeDecodeChunk(&encoder, &decoder);

  // Setup data.
  const std::string str1_orig = "[0.0] Fuchsia lz4 encoding test log line 1\n";
  const std::string str2_orig = "[0.0] Fuchsia lz4 encoding test log line 2\n";
  const std::string str3_orig = "[0.0] Fuchsia lz4 encoding test log line 3\n";

  // Reconstruct string by encoding and decoding.
  const std::string str1_rec = EncodeDecodeChunk(str1_orig);
  const std::string str2_rec = EncodeDecodeChunk(str2_orig);
  const std::string str3_rec = EncodeDecodeChunk(str3_orig);

  // Test contents.
  EXPECT_EQ(str1_orig, str1_rec);
  EXPECT_EQ(str2_orig, str2_rec);
  EXPECT_EQ(str3_orig, str3_rec);
}

std::string GenerateRandomData(int seed, uint16_t length) {
  std::string output = "";
  srand(seed);
  while (output.size() < length) {
    output += (char)(rand() & 0xFF);
  }
  return output;
}

TEST(EncodingTest, EncodeDecodeChunk_RecallTest) {
  // This test provides random 32B data strings to the encoder and decoder and then recalls them
  // back in reverse order. This tests data loss due to incorrect recalls. The data provided is
  // more than the LZ4 buffer size = 64KB.
  const uint16_t chunk_size = 32;
  const uint16_t chunk_num = 2048;
  auto encoder = Lz4Encoder();
  auto decoder = Lz4ChunkDecoder();
  auto EncodeDecodeChunk = MakeEncodeDecodeChunk(&encoder, &decoder);

  // Set data
  for (uint16_t idx = 0; idx < chunk_num; idx++) {
    const std::string str_orig = GenerateRandomData(idx, chunk_size);
    const std::string str_rec = EncodeDecodeChunk(str_orig);
    const std::string line_number = fxl::StringPrintf("[Set] line number {%d}:", idx);

    ASSERT_EQ(line_number + str_orig, line_number + str_rec);
  }

  // Recall
  for (int idx = chunk_num - 1; idx >= 0; idx--) {
    const std::string str_orig = GenerateRandomData(idx, chunk_size);
    const std::string str_rec = EncodeDecodeChunk(str_orig);
    const std::string line_number = fxl::StringPrintf("[Recall] line number {%d}:", idx);

    ASSERT_EQ(line_number + str_orig, line_number + str_rec);
  }
}

TEST(EncodingTest, TestEncodeDecodeMsgBlock) {
  // Choose encoder and decoder.
  auto encoder = Lz4Encoder();
  auto decoder = Lz4Decoder();

  // Setup data.
  const std::string str1_orig = "[0.0] Fuchsia lz4 encoding test log line 1\n";
  const std::string str2_orig = "[0.0] Fuchsia lz4 encoding test log line 2\n";
  const std::string str3_orig = "[0.0] Fuchsia lz4 encoding test log line 3\n";
  const std::string original_message = str1_orig + str2_orig + str3_orig;

  std::string block;
  block += encoder.Encode(str1_orig);
  block += encoder.Encode(str2_orig);
  block += encoder.Encode(str3_orig);

  auto decoded = decoder.Decode(block);

  // Test contents.
  EXPECT_EQ(decoded, original_message);
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
