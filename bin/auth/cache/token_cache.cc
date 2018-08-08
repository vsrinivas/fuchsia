// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "token_cache.h"

#include "lib/fidl/cpp/string.h"
#include "lib/fxl/time/time_point.h"

namespace auth {
namespace cache {

TokenCache::TokenCache(int cache_size) : cache_map_(cache_size) {}

Status TokenCache::Get(const CacheKey& key, OAuthTokens* tokens_out) {
  return cache_map_.Fetch(key, tokens_out);
}

Status TokenCache::Put(const CacheKey& key, const OAuthTokens& tokens) {
  return cache_map_.Insert(key, tokens);
}

Status TokenCache::Delete(const CacheKey& key) {
  return cache_map_.Delete(key);
}

Status TokenCache::AddFirebaseToken(const CacheKey& key,
                                    const std::string& firebase_api_key,
                                    const FirebaseAuthToken firebase_token) {
  if (!firebase_token.IsValid()) {
    return Status::kInvalidArguments;
  }

  if (!HasKey(key)) {
    return Status::kKeyNotFound;
  }

  OAuthTokens tokens_out;
  auto status = cache_map_.Fetch(key, &tokens_out);
  if (status != Status::kOK) {
    return status;
  }

  tokens_out.firebase_tokens_map[firebase_api_key] = std::move(firebase_token);
  cache_map_.Insert(key, std::move(tokens_out));

  return Status::kOK;
}

bool TokenCache::HasKey(const CacheKey& key) { return cache_map_.HasKey(key); }

}  // namespace cache
}  // namespace auth
