// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/crypto/hmac.h"

#include <gtest/gtest.h>

#include "apps/ledger/src/glue/crypto/crypto_test_util.h"

namespace glue {
namespace {

TEST(HMAC, Correctness) {
  std::string key = FromHex(
      "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20212223"
      "2425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F");
  std::string message = "Sample message for keylen=blocklen";
  std::string expected = FromHex(
      "8BB9A1DB9806F20DF7F77B82138C7914D174D59E13DC4D0169C9057B133E1D62");
  EXPECT_EQ(32u, expected.size());
  EXPECT_EQ(expected, SHA256HMAC(key, message));
}
}  // namespace
}  // namespace glue
