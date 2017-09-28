// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_CACHE_TOKEN_CACHE_H
#define GARNET_BIN_AUTH_CACHE_TOKEN_CACHE_H

#include <list>
#include <map>
#include <string>
#include <utility>

#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

// Token cache interface for an in-memory cache for storing most frequently used
// short lived tokens such as OAuth Access Tokens, ID Tokens and Firebase Auth
// Tokens. Token cache uses |CacheKey| as the key that uniquely combines User
// Identifier with the Identity Provider and the credential received from the
// Identity Provider. Each CacheKey stores a set of |OAuthTokens| that contains
// token creation timestamp, expiration time, and the actual value of the token.
//
// Token cache implements LRU Cache functionality where both insert and remove
// operations are done in O(lgN) using a map and a list implementation.

namespace auth {
namespace cache {

// Adjusts the token expiration window by a small amount to proactively refresh
// tokens before the expiry time limit has reached.
constexpr fxl::TimeDelta kPaddingForTokenExpiry =
    fxl::TimeDelta::FromSeconds(600);

// The status of an operation.
enum Status {
  // The operation succeeded.
  kOK = 0,

  // The operation was not attempted because the arguments are invalid.
  kInvalidArguments,

  // The operation was not attempted because the given key is not found in
  // cache.
  kKeyNotFound,

  // The operation was attempted but failed because the entry in cache has
  // expired.
  kCacheExpired,

  // The operation was attempted but failed for an unspecified reason. More
  // information may be found in the log file.
  kOperationFailed,
};

// Unique key for accessing token cache.
struct CacheKey {
  const std::string user_id;
  const std::string idp_provider;
  const std::string idp_credential_id;

  CacheKey(std::string user_id,
           std::string idp_provider,
           std::string idp_credential_id)
      : user_id(std::move(user_id)),
        idp_provider(std::move(idp_provider)),
        idp_credential_id(std::move(idp_credential_id)) {}

  bool operator<(const CacheKey& other) const {
    return std::tie(user_id, idp_provider, idp_credential_id) <
           std::tie(other.user_id, other.idp_provider, other.idp_credential_id);
  }

  bool operator==(const CacheKey& other) const {
    return (this->user_id == other.user_id &&
            this->idp_provider == other.idp_provider &&
            this->idp_credential_id == other.idp_credential_id);
  }

  bool IsValid() const {
    return !(user_id.empty() || idp_provider.empty() ||
             idp_credential_id.empty());
  }
};

// In-memory cache for short lived firebase auth id-tokens. These tokens get
// reset on system reboots. Tokens are cached based on the expiration time
// set by the Firebase servers. Cache is indexed by firebase api keys.
struct FirebaseAuthToken {
  fxl::TimePoint expiration_time;
  std::string fb_id_token;
  std::string local_id;
  std::string email;

  bool operator==(const FirebaseAuthToken& other) const {
    return (this->expiration_time == other.expiration_time &&
            this->fb_id_token == other.fb_id_token &&
            this->local_id == other.local_id && this->email == other.email);
  }

  bool IsValid() const {
    return expiration_time > fxl::TimePoint::Min() && !fb_id_token.empty() &&
           !local_id.empty();
  }

  // Returns true if the stored token has expired.
  bool HasExpired() const {
    FXL_DCHECK(IsValid());
    return (expiration_time - fxl::TimePoint::Now()) < kPaddingForTokenExpiry;
  }
};

// In-memory cache for short lived oauth tokens that resets on system reboots.
// Tokens are cached based on the expiration time set by the Identity provider.
// Token cache is indexed by unique |CacheKey|.
struct OAuthTokens {
  fxl::TimePoint expiration_time;
  std::string access_token;
  std::string id_token;
  std::map<std::string, FirebaseAuthToken> firebase_tokens_map;

  bool IsValid() const {
    return (expiration_time > fxl::TimePoint::Min()) &&
           !(access_token.empty() || id_token.empty());
  }

  // Returns true if the stored token has expired.
  bool HasExpired() const {
    FXL_DCHECK(IsValid());
    return (expiration_time - fxl::TimePoint::Now()) < kPaddingForTokenExpiry;
  }
};

class LinkedHashMap {
 public:
  LinkedHashMap(int cache_size) { cache_size_ = cache_size; }

  Status Insert(const CacheKey& key, const OAuthTokens& tokens) {
    if (!key.IsValid() || !tokens.IsValid()) {
      return Status::kInvalidArguments;
    }

    auto it = tokens_map_.find(key);
    if (it != tokens_map_.end()) {
      tokens_list_.erase(it->second);
      tokens_map_.erase(it);
    }

    tokens_list_.push_front(std::make_pair(key, tokens));
    tokens_map_.insert(std::make_pair(key, tokens_list_.begin()));

    // readjust tokens_map_ for max cache_size
    while (tokens_map_.size() > cache_size_) {
      auto last_it = tokens_list_.end();
      last_it--;
      tokens_map_.erase(last_it->first);
      tokens_list_.pop_back();
    }

    return Status::kOK;
  }

  Status Fetch(const CacheKey& key, OAuthTokens* tokens_out) {
    FXL_CHECK(tokens_out);

    if (!key.IsValid()) {
      return Status::kInvalidArguments;
    }

    auto it = tokens_map_.find(key);
    if (it == tokens_map_.end()) {
      return Status::kKeyNotFound;
    }

    // check if the cache has expired before returning. If oauth tokens are
    // expired, remove them from cache before returning error status.
    if (it->second->second.HasExpired()) {
      Delete(key);
      return Status::kCacheExpired;
    }

    tokens_list_.splice(tokens_list_.begin(), tokens_list_, it->second);
    auto& tokens = it->second->second;

    // OAuth token is valid, check to see if firebase tokens are all also valid.
    // Purge all expired firebase tokens before returning.
    for (auto fbtoken_itr = tokens.firebase_tokens_map.begin();
         fbtoken_itr != tokens.firebase_tokens_map.end(); ++fbtoken_itr) {
      if (fbtoken_itr->second.HasExpired()) {
        tokens.firebase_tokens_map.erase(fbtoken_itr);
      }
    }

    *tokens_out = tokens;

    return Status::kOK;
  }

  Status Delete(const CacheKey& key) {
    if (!key.IsValid()) {
      return Status::kInvalidArguments;
    }

    auto it = tokens_map_.find(key);
    if (it == tokens_map_.end()) {
      return Status::kKeyNotFound;
    }

    tokens_list_.erase(it->second);
    tokens_map_.erase(it);
    return Status::kOK;
  }

  bool HasKey(const CacheKey& key) { return tokens_map_.count(key) > 0; }

 private:
  // List of entries in the cache stored as pairs of |CacheKey| and
  // |OAuthTokens|. The most recently used one is always found at the beginning
  // of the list and the least recently used at the tail end.
  std::list<std::pair<CacheKey, OAuthTokens>> tokens_list_;
  // Map with keys as |CacheKey| and an iterator value pointing to the
  // beginning of the above list that implements an LRU based cache.
  std::map<CacheKey, decltype(tokens_list_.begin())> tokens_map_;
  // Max size of cache
  size_t cache_size_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkedHashMap);
};

class TokenCache {
 public:
  // Initializes token cache with a capacity of |cache_size|.
  TokenCache(int cache_size);

  // Returns all unexpired tokens stored in cache in |tokens_out| for the given
  // |key|. Expired tokens are also purged from the underlying cache. These
  // include both OAuthTokens and FirebaseAuthTokens.
  //
  // Returns kOK on success or an error status on failure.
  Status Get(const CacheKey& key, OAuthTokens* tokens_out);

  // Adds a new cache entry for the cache key |key| and sets it with the given
  // list of tokens |tokens|.
  //
  // Returns kOK on success or an error status on failure.
  Status Put(const CacheKey& key, const OAuthTokens& tokens);

  // Removes all tokens indexed by cache key |key| from the token cache.
  Status Delete(const CacheKey& key);

  // Adds a new firebase auth token |firebase_token| for api key
  // |firebase_api_key| to an existing cache entry identified by |key|.
  //
  // Returns kOK on success or an error status on failure.
  Status AddFirebaseToken(const CacheKey& key,
                          const std::string& firebase_api_key,
                          const FirebaseAuthToken firebase_token);

  // Returns true if |key| was found in the token cache.
  bool HasKey(const CacheKey& key);

 private:
  LinkedHashMap cache_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenCache);
};

}  // namespace cache
}  // namespace auth

#endif  // GARNET_BIN_AUTH_CACHE_TOKEN_CACHE_H
