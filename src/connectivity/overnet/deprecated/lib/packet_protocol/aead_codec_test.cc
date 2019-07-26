// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/packet_protocol/aead_codec.h"

#include <random>

#include "gtest/gtest.h"

namespace overnet {
namespace aead_codec_test {

struct TestArgs {
  const EVP_AEAD* aead;
  std::vector<uint64_t> seqs;
  Slice payload;
};

class AEADCodec : public ::testing::TestWithParam<TestArgs> {};

TEST_P(AEADCodec, Basics) {
  std::vector<uint8_t> key;
  std::random_device rng;
  for (size_t i = 0; i < EVP_AEAD_key_length(GetParam().aead); i++) {
    key.push_back(rng());
  }

  const char* ad = "HELLO!";

  overnet::AEADCodec codec1(GetParam().aead, key.data(), key.size(),
                            reinterpret_cast<const uint8_t*>(ad), strlen(ad));
  overnet::AEADCodec codec2(GetParam().aead, key.data(), key.size(),
                            reinterpret_cast<const uint8_t*>(ad), strlen(ad));

  std::vector<Slice> encoded;
  for (uint64_t seq : GetParam().seqs) {
    auto enc = codec1.Encode(seq, GetParam().payload);
    ASSERT_TRUE(enc.is_ok()) << enc;
    encoded.push_back(*enc);
    EXPECT_NE(*enc, GetParam().payload);
    auto dec = codec2.Decode(seq, *enc);
    ASSERT_TRUE(dec.is_ok()) << dec;
    EXPECT_NE(*dec, encoded.back());
    EXPECT_EQ(*dec, GetParam().payload);
  }

  for (size_t i = 0; i < encoded.size(); i++) {
    for (size_t j = i + 1; j < encoded.size(); j++) {
      EXPECT_NE(encoded[i], encoded[j]) << "i=" << i << " j=" << j;
    }
  }
}

const auto kTestCases = [] {
  std::vector<TestArgs> out;
  for (auto aead : {
           EVP_aead_aes_128_gcm(),
           EVP_aead_aes_256_gcm(),
           EVP_aead_chacha20_poly1305(),
           EVP_aead_xchacha20_poly1305(),
           EVP_aead_aes_128_ctr_hmac_sha256(),
           EVP_aead_aes_256_ctr_hmac_sha256(),
           EVP_aead_aes_128_gcm_siv(),
           EVP_aead_aes_256_gcm_siv(),
       }) {
    out.push_back(TestArgs{
        aead, {1, 2, 3, 5, 8, 13, 21, 34}, Slice::FromContainer({1, 2, 3, 4, 5, 6, 7, 8})});
    out.push_back(
        TestArgs{aead, {0x123456789abcdefull, 123, 321}, Slice::RepeatedChar(1024 * 1024, 'a')});
  }
  return out;
}();

INSTANTIATE_TEST_SUITE_P(AEADCodecTest, AEADCodec,
                         ::testing::ValuesIn(kTestCases.begin(), kTestCases.end()));

}  // namespace aead_codec_test
}  // namespace overnet
