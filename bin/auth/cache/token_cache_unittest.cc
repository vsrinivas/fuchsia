// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "token_cache.h"

#include <stdlib.h>
#include <list>
#include <string>
#include <unordered_map>

#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

#include "gtest/gtest.h"

using auth::cache::TokenCache;

namespace auth {
namespace cache {

namespace {

constexpr int kMaxCacheSize = 10;
constexpr int kDefaultTokenExpiration = 3600;

CacheKey MakeCacheKey(const int index) {
  std::string user_id(15, 0);
  std::snprintf(&user_id[0], user_id.size(), "user_id_%d", index);

  std::string idp(15, 0);
  std::snprintf(&idp[0], idp.size(), "idp_%d", index);

  std::string idp_cred_id(15, 0);
  std::snprintf(&idp_cred_id[0], idp_cred_id.size(), "idp_cred_id_%d", index);

  return CacheKey(user_id, idp, idp_cred_id);
}

FirebaseAuthToken MakeFirebaseAuthToken(const int index, const int expires_in) {
  FirebaseAuthToken fb_token;
  fb_token.expiration_time =
      fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(expires_in);
  fb_token.fb_id_token = "fb_id_token_" + std::to_string(index);
  fb_token.local_id = "local_id_" + std::to_string(index);
  fb_token.email = "email_@" + std::to_string(index);

  return fb_token;
}

OAuthTokens MakeOAuthTokens(const int index, const int expires_in) {
  std::string access_token(15, 0);
  std::snprintf(&access_token[0], access_token.size(), "access_token_%d",
                index);

  std::string id_token(15, 0);
  std::snprintf(&id_token[0], id_token.size(), "id_token_%d", index);

  OAuthTokens tokens;
  tokens.expiration_time =
      fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(expires_in);
  tokens.access_token = access_token;
  tokens.id_token = id_token;

  for (int i = 0; i < index; i++) {
    tokens.firebase_tokens_map["fb_api_key_" + std::to_string(i)] =
        MakeFirebaseAuthToken(i, expires_in);
  }

  return tokens;
}

void VerifyFirebaseAuthToken(const FirebaseAuthToken& expected,
                             const FirebaseAuthToken& got) {
  EXPECT_EQ(expected.expiration_time, got.expiration_time);
  EXPECT_EQ(expected.fb_id_token, got.fb_id_token);
  EXPECT_EQ(expected.local_id, got.local_id);
  EXPECT_EQ(expected.email, got.email);
}

bool VerifyOAuthTokens(const OAuthTokens& expected, const OAuthTokens& got) {
  EXPECT_EQ(expected.expiration_time, got.expiration_time);
  EXPECT_EQ(expected.access_token, got.access_token);
  EXPECT_EQ(expected.id_token, got.id_token);

  for (std::pair<std::string, FirebaseAuthToken> fb_token :
       expected.firebase_tokens_map) {
    if (got.firebase_tokens_map.find(fb_token.first) !=
        got.firebase_tokens_map.end()) {
      VerifyFirebaseAuthToken(fb_token.second,
                              got.firebase_tokens_map.at(fb_token.first));
      continue;
    } else {
      return false;
    }
  }

  return true;
}

}  // namespace

class AuthCacheTest : public ::testing::Test {
 protected:
  AuthCacheTest() {}

  ~AuthCacheTest() override {}
};

TEST_F(AuthCacheTest, CheckFirebaseAuthToken) {
  EXPECT_FALSE(
      (FirebaseAuthToken{fxl::TimePoint::Min(), "a", "a", "a"}).IsValid());
  EXPECT_FALSE(
      (FirebaseAuthToken{fxl::TimePoint::Now(), "", "a", "a"}).IsValid());
  EXPECT_FALSE(
      (FirebaseAuthToken{fxl::TimePoint::Now(), "a", "", "a"}).IsValid());
  EXPECT_TRUE(
      (FirebaseAuthToken{fxl::TimePoint::Now(), "a", "a", ""}).IsValid());

  FirebaseAuthToken token1{
      fxl::TimePoint::Now() - fxl::TimeDelta::FromSeconds(7200), "a", "a", "a"};
  EXPECT_TRUE(token1.IsValid());
  EXPECT_TRUE(token1.HasExpired());

  FirebaseAuthToken token2{
      fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(7200), "a", "a", "a"};
  EXPECT_TRUE(token1.IsValid());
  EXPECT_FALSE(token2.HasExpired());
}

TEST_F(AuthCacheTest, CheckOAuthTokens) {
  std::map<std::string, FirebaseAuthToken> firebase_tokens_map;
  EXPECT_FALSE(
      (OAuthTokens{fxl::TimePoint::Min(), "a", "a", firebase_tokens_map})
          .IsValid());
  EXPECT_FALSE((OAuthTokens{fxl::TimePoint::Now(), "", "", firebase_tokens_map})
                   .IsValid());
  EXPECT_FALSE(
      (OAuthTokens{fxl::TimePoint::Now(), "", "a", firebase_tokens_map})
          .IsValid());
  EXPECT_FALSE(
      (OAuthTokens{fxl::TimePoint::Now(), "a", "", firebase_tokens_map})
          .IsValid());
  EXPECT_TRUE(
      (OAuthTokens{fxl::TimePoint::Now(), "a", "a", firebase_tokens_map})
          .IsValid());

  OAuthTokens otokens1{
      fxl::TimePoint::Now() - fxl::TimeDelta::FromSeconds(7200), "a", "a",
      firebase_tokens_map};
  EXPECT_TRUE(otokens1.IsValid());
  EXPECT_TRUE(otokens1.HasExpired());

  OAuthTokens otokens2{
      fxl::TimePoint::Now() + fxl::TimeDelta::FromSeconds(7200), "a", "a",
      firebase_tokens_map};
  EXPECT_TRUE(otokens2.IsValid());
  EXPECT_FALSE(otokens2.HasExpired());
}

TEST_F(AuthCacheTest, CheckGetAndPut) {
  TokenCache cache(kMaxCacheSize);

  // check for cache miss
  auto key = CacheKey("u1", "idp1", "cred_id1");
  OAuthTokens out;
  auto status = cache.Get(key, &out);
  EXPECT_EQ(status, Status::kKeyNotFound);

  // populate cache for kMaxCacheSize entries
  OAuthTokens expectedTokens[kMaxCacheSize];
  for (int i = 0; i < kMaxCacheSize; i++) {
    expectedTokens[i] = MakeOAuthTokens(i, kDefaultTokenExpiration);
    EXPECT_EQ(Status::kOK, cache.Put(MakeCacheKey(i), expectedTokens[i]));
  }

  // Fetch and verify all cache entries
  for (int i = 0; i < kMaxCacheSize; i++) {
    OAuthTokens token;
    EXPECT_EQ(Status::kOK, cache.Get(MakeCacheKey(i), &token));
    VerifyOAuthTokens(expectedTokens[i], token);
  }
}

TEST_F(AuthCacheTest, CheckExpiredTokens) {
  TokenCache cache(kMaxCacheSize);

  // populate cache with both expired and unexpired entries.
  // Any expiry time that is less than |kPaddingForTokenExpiry| is considered
  // expired.
  OAuthTokens expectedTokens[kMaxCacheSize];
  for (int i = 0; i < kMaxCacheSize / 2; i++) {
    expectedTokens[i] = MakeOAuthTokens(i, i);
    EXPECT_EQ(Status::kOK, cache.Put(MakeCacheKey(i), expectedTokens[i]));
  }
  for (int i = kMaxCacheSize / 2; i < kMaxCacheSize; i++) {
    expectedTokens[i] =
        MakeOAuthTokens(i, i + auth::cache::kPaddingForTokenExpiry.ToSeconds());
    EXPECT_EQ(Status::kOK, cache.Put(MakeCacheKey(i), expectedTokens[i]));
  }

  // Fetch and verify all cache entries
  for (int i = 0; i < kMaxCacheSize; i++) {
    OAuthTokens token;
    if (i < kMaxCacheSize / 2) {
      EXPECT_EQ(Status::kCacheExpired, cache.Get(MakeCacheKey(i), &token));
    } else {
      EXPECT_EQ(Status::kOK, cache.Get(MakeCacheKey(i), &token));
      VerifyOAuthTokens(expectedTokens[i], token);
    }
  }
}

TEST_F(AuthCacheTest, CheckAddAndModifyFirebaseToken) {
  TokenCache cache(kMaxCacheSize);

  // populate cache for kMaxCacheSize entries
  OAuthTokens expectedTokens[kMaxCacheSize];
  for (int i = 0; i < kMaxCacheSize; i++) {
    expectedTokens[i] = MakeOAuthTokens(i, kDefaultTokenExpiration);
    EXPECT_EQ(Status::kOK, cache.Put(MakeCacheKey(i), expectedTokens[i]));
  }

  // Update a new firebase token to an existing firebase_api_key.
  int update_index = 7;
  auto update_cache_key = MakeCacheKey(update_index);
  std::string update_api_key = "fb_api_key_" + std::to_string(1);
  auto fb_token_77 = MakeFirebaseAuthToken(77, kDefaultTokenExpiration);
  EXPECT_EQ(Status::kOK, cache.AddFirebaseToken(update_cache_key,
                                                update_api_key, fb_token_77));

  // Add new firebase token for a new firebase_api_key
  auto fb_token_88 = MakeFirebaseAuthToken(88, kDefaultTokenExpiration);
  std::string new_api_key = "fb_api_key_" + std::to_string(888);
  EXPECT_EQ(Status::kOK,
            cache.AddFirebaseToken(update_cache_key, new_api_key, fb_token_88));

  // Add new firebase token for a new firebase_api_key that will expire shortly.
  std::string expired_api_key = "fb_api_key_" + std::to_string(999);
  EXPECT_EQ(Status::kOK,
            cache.AddFirebaseToken(
                update_cache_key, expired_api_key,
                MakeFirebaseAuthToken(
                    99, auth::cache::kPaddingForTokenExpiry.ToSeconds() - 10)));

  OAuthTokens new_tokens_out;
  EXPECT_EQ(Status::kOK, cache.Get(update_cache_key, &new_tokens_out));

  // verify firebase token map size for all token operations - update, new and
  // expired.
  EXPECT_EQ(
      expectedTokens[update_index].firebase_tokens_map.count(update_api_key),
      new_tokens_out.firebase_tokens_map.count(update_api_key));
  EXPECT_EQ(1, int(new_tokens_out.firebase_tokens_map.count(new_api_key)));
  EXPECT_EQ(0, int(new_tokens_out.firebase_tokens_map.count(expired_api_key)));

  // verify firebase token contents
  VerifyFirebaseAuthToken(fb_token_77,
                          new_tokens_out.firebase_tokens_map[update_api_key]);
  VerifyFirebaseAuthToken(fb_token_88,
                          new_tokens_out.firebase_tokens_map[new_api_key]);
}

TEST_F(AuthCacheTest, CheckLRUFetch) {
  const int kMaxCacheSize = 10;
  TokenCache cache(kMaxCacheSize);

  // populate cache for kMaxCacheSize entries
  for (int i = 0; i < kMaxCacheSize; i++) {
    EXPECT_EQ(Status::kOK,
              cache.Put(MakeCacheKey(i),
                        MakeOAuthTokens(i, kDefaultTokenExpiration)));
  }

  // Add new entry to existing cache exceeding max cache size.
  for (int i = kMaxCacheSize; i < kMaxCacheSize * 2; i++) {
    auto key = MakeCacheKey(i);
    EXPECT_EQ(Status::kOK,
              cache.Put(key, MakeOAuthTokens(i, kDefaultTokenExpiration)));

    // For each new entry, least recently used entry is kicked out and get fails
    // on that key.
    int missing_key_index = i - kMaxCacheSize;
    OAuthTokens out;
    EXPECT_EQ(Status::kKeyNotFound,
              cache.Get(MakeCacheKey(missing_key_index), &out));
  }

  // Tests that reading an old entry puts it back in the front of the queue.
  // Least recently used in the queue so far is for key index "kMaxCacheSize".
  // So, this entry will be kicked out from cache only after next
  // (kMaxCacheSize - 1) new entries.
  OAuthTokens out;
  EXPECT_EQ(Status::kOK, cache.Get(MakeCacheKey(kMaxCacheSize), &out));
  for (int i = 0; i < kMaxCacheSize; i++) {
    auto key = MakeCacheKey(i);
    EXPECT_EQ(Status::kOK,
              cache.Put(key, MakeOAuthTokens(i, kDefaultTokenExpiration)));
    if (i <= kMaxCacheSize - 1) {
      EXPECT_EQ(Status::kOK, cache.Get(MakeCacheKey(kMaxCacheSize), &out));
    } else {
      EXPECT_EQ(Status::kKeyNotFound,
                cache.Get(MakeCacheKey(kMaxCacheSize), &out));
    }
  }
}

}  // namespace cache
}  // namespace auth

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
