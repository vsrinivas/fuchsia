// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/primitives/encrypt.h"

#include <zircon/syscalls.h>

#include <algorithm>

#include <gtest/gtest.h>

#include "peridot/lib/rng/test_random.h"

namespace encryption {
namespace {

using EncryptTest = ::testing::TestWithParam<size_t>;

TEST_P(EncryptTest, Correctness) {
  const size_t kMessageSize = GetParam();
  rng::TestRandom random(0);

  std::string key;
  key.resize(16);
  random.Draw(&key);
  std::string message;
  message.resize(kMessageSize);
  random.Draw(&message);

  std::string encrypted;
  std::string decrypted;
  ASSERT_TRUE(AES128GCMSIVEncrypt(&random, key, message, &encrypted));

  // Check that decrypted is the original message.
  ASSERT_TRUE(AES128GCMSIVDecrypt(key, encrypted, &decrypted));
  EXPECT_EQ(decrypted, message);

  // Check that changing any of the first 128 bytes breaks the encryption.
  for (size_t index = 0; index < std::min<size_t>(128u, encrypted.size()); ++index) {
    std::string encrypted_copy = encrypted;
    encrypted_copy[index] ^= 0xFF;
    EXPECT_FALSE(AES128GCMSIVDecrypt(key, encrypted_copy, &decrypted));
  }
}

INSTANTIATE_TEST_SUITE_P(, EncryptTest, ::testing::Values(0, 64, 127, 128, 129, 192, 256, 12345));

}  // namespace
}  // namespace encryption
