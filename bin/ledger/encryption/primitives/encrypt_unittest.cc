// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/encrypt.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "lib/fxl/random/rand.h"

namespace encryption {
namespace {

using EncryptTest = ::testing::TestWithParam<size_t>;

TEST_P(EncryptTest, Correctness) {
  const size_t kMessageSize = GetParam();

  std::string key;
  key.resize(16);
  ASSERT_TRUE(fxl::RandBytes(&key[0], key.size()));
  std::string message;
  message.resize(kMessageSize);
  ASSERT_TRUE(fxl::RandBytes(&message[0], message.size()));

  std::string encrypted;
  std::string decrypted;
  ASSERT_TRUE(AES128GCMSIVEncrypt(key, message, &encrypted));

  // Check that decrypted is the original message.
  ASSERT_TRUE(AES128GCMSIVDecrypt(key, encrypted, &decrypted));
  EXPECT_EQ(message, decrypted);

  // Check that changing any of the first 128 bytes breaks the encryption.
  for (size_t index = 0; index < std::min<size_t>(128u, encrypted.size());
       ++index) {
    std::string encrypted_copy = encrypted;
    encrypted_copy[index] ^= 0xFF;
    EXPECT_FALSE(AES128GCMSIVDecrypt(key, encrypted_copy, &decrypted));
  }
}

INSTANTIATE_TEST_CASE_P(, EncryptTest,
                        ::testing::Values(0, 64, 127, 128, 129, 192, 256,
                                          12345));

}  // namespace
}  // namespace encryption
