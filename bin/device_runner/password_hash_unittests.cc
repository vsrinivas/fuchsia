// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/rand.h>

#include "apps/modular/src/device_runner/password_hash.h"
#include "gtest/gtest.h"

namespace {

std::string GetPassword(size_t size) {
  std::string password;
  password.resize(size);
  RAND_bytes(reinterpret_cast<uint8_t*>(&password[0]), password.size());
  return password;
}

TEST(PasswordHash, Comparison) {
  for (size_t size = 19; size < 20; ++size) {
    std::string password = GetPassword(size);
    RAND_bytes(reinterpret_cast<uint8_t*>(&password[0]), password.size());

    std::string hash1;
    std::string hash2;

    EXPECT_TRUE(modular::HashPassword(password, &hash1));
    EXPECT_TRUE(modular::HashPassword(password, &hash2));

    EXPECT_EQ(hash1.size(), hash2.size());
    EXPECT_NE(hash1, hash2);
    EXPECT_TRUE(modular::CheckPassword(password, hash1));
    EXPECT_TRUE(modular::CheckPassword(password, hash2));
  }
}

TEST(PasswordHash, Seed) {
  for (size_t password_size = 0; password_size < 10; ++password_size) {
    std::string password = GetPassword(password_size);
    for (size_t seed_size = 1; seed_size < 10; ++seed_size) {
      std::string seed = GetPassword(seed_size);
      std::string hash1;
      std::string hash2;
      EXPECT_TRUE(modular::HashPassword(password, &hash1, seed));
      EXPECT_TRUE(modular::HashPassword(password, &hash2, seed));
      EXPECT_EQ(hash1, hash2);
    }
  }
}

}  // namespace
