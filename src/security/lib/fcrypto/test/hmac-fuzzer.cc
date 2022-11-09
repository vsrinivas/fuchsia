// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/hmac.h"

static const size_t kMaxKeyLen = 1024;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider fuzzed_data(data, size);
  auto digest = fuzzed_data.PickValueInArray<crypto::digest::Algorithm>(
      {crypto::digest::kSHA256, crypto::digest::kUninitialized});

  // Picking Flags:
  // There's a short circuit for invalid flags, so we pick between the following
  // options:
  // { NO_FLAGS, HMAC::ALLOW_TRUNCATION, HMAC::ALLOW_WEAK_KEY, BOTH, RANDOM }
  // The goal is to get valid flags most of the time, but allow for sometimes
  // picking a completely random flag. That way, if in the future the
  // short-circuit logic is removed, or more flags are added, the fuzzer might
  // still be able to fuzz those flags.
  auto flags = fuzzed_data.PickValueInArray<uint16_t>(
      {0, crypto::HMAC::ALLOW_TRUNCATION, crypto::HMAC::ALLOW_WEAK_KEY,
       crypto::HMAC::ALLOW_TRUNCATION | crypto::HMAC::ALLOW_WEAK_KEY,
       fuzzed_data.ConsumeIntegral<uint16_t>()});

  const auto key_data =
      fuzzed_data.ConsumeBytes<uint8_t>(fuzzed_data.ConsumeIntegralInRange<size_t>(1, kMaxKeyLen));
  if (key_data.size() == 0) {
    return 0;
  }
  crypto::Secret key;
  uint8_t* key_ptr = nullptr;

  key.Allocate(key_data.size(), &key_ptr);
  memcpy(key_ptr, key_data.data(), key_data.size());

  crypto::Bytes hmac;
  auto hmac_data = fuzzed_data.ConsumeRemainingBytes<uint8_t>();
  zx_status_t res =
      crypto::HMAC::Create(digest, key, hmac_data.data(), hmac_data.size(), &hmac, flags);
  if (res != ZX_OK) {
    return 0;
  }

  res = crypto::HMAC::Verify(digest, key, hmac_data.data(), hmac_data.size(), hmac, flags);
  assert(res == ZX_OK);
  return 0;
}
