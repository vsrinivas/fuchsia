// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/device_runner/password_hash.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace modular {

namespace {

constexpr size_t kHashIdentifierSize = 6;
constexpr ftl::StringView kSha256Identifier = "SHA256";
constexpr uint8_t kSeedSize = 8;

static_assert(kHashIdentifierSize == kSha256Identifier.size(), "Hash identifier size is incorrect");

}  // namespace

bool HashPassword(const std::string& password,
                  std::string* result,
                  ftl::StringView seed) {
  std::string tmp_result;
  tmp_result.resize(kHashIdentifierSize + 1 +
                    (seed.empty() ? kSeedSize : seed.size()) +
                    SHA256_DIGEST_LENGTH);
  uint8_t* out = reinterpret_cast<uint8_t*>(&tmp_result[0]);
  memcpy(out, kSha256Identifier.data(), kSha256Identifier.size());
  out += kSha256Identifier.size();
  size_t seed_size = seed.size();
  if (seed_size != 0) {
    *out = seed.size();
    ++out;
    memcpy(out, seed.data(), seed.size());
  } else {
    seed_size = kSeedSize;
    *out = kSeedSize;
    ++out;
    if (RAND_bytes(out, kSeedSize) != 1) {
      return false;
    }
  }
  SHA256_CTX sha_ctx;
  if (SHA256_Init(&sha_ctx) != 1) {
    return false;
  }
  if (SHA256_Update(&sha_ctx, out, seed_size) != 1) {
    return false;
  }
  out += seed_size;
  if (password.size() > 0) {
    if (SHA256_Update(&sha_ctx, password.data(), password.size()) != 1) {
      return false;
    }
  }
  if (SHA256_Final(out, &sha_ctx) != 1) {
    return false;
  }

  result->swap(tmp_result);
  return true;
}

bool CheckPassword(const std::string& password, const std::string& hash) {
  ftl::StringView hash_view = hash;
  if (hash_view.substr(0, kSha256Identifier.size()) != kSha256Identifier) {
    FTL_LOG(WARNING) << "Hash method unknown";
    return false;
  }
  ftl::StringView remaining = hash_view.substr(kSha256Identifier.size());
  if (remaining.empty()) {
    FTL_LOG(WARNING) << "Incorrect hash.";
    return false;
  }
  uint8_t seed_size = remaining[0];
  remaining = remaining.substr(1);
  if (remaining.size() != seed_size + SHA256_DIGEST_LENGTH) {
    FTL_LOG(WARNING) << "Incorrect hash.";
    return false;
  }
  ftl::StringView seed = remaining.substr(0, seed_size);
  std::string computed_hash;
  if (!HashPassword(password, &computed_hash, seed)) {
    FTL_LOG(WARNING) << "Unable to compute hash.";
    return false;
  }
  return hash == computed_hash;
}

}  // namespace modular
