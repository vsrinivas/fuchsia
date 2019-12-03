// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/token_manager.h"

#include <fuchsia/ledger/cpp/fidl.h>

#include <random>

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/fake_disk_cleanup_manager.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"

namespace ledger {
namespace {

class TokenManagerTest : public TestWithEnvironment {
 public:
  TokenManagerTest() = default;
  TokenManagerTest(const TokenManagerTest&) = delete;
  TokenManagerTest& operator=(const TokenManagerTest&) = delete;
  ~TokenManagerTest() override = default;

 protected:
  TokenManager token_manager_;
};

TEST_F(TokenManagerTest, SingleExpiringTokenImmediatelyDiscarded) {
  bool on_discardable_called;

  token_manager_.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  token_manager_.CreateToken();

  EXPECT_TRUE(token_manager_.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(TokenManagerTest, SingleExpiringTokenNotImmediatelyDiscarded) {
  bool on_discardable_called;

  token_manager_.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  {
    auto expiring_token = token_manager_.CreateToken();

    EXPECT_FALSE(token_manager_.IsDiscardable());
    EXPECT_FALSE(on_discardable_called);
  }
  EXPECT_TRUE(token_manager_.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(TokenManagerTest, MultipleExpiringTokensNotImmediatelyDiscarded) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  int token_count = std::uniform_int_distribution(2, 20)(bit_generator);
  bool on_discardable_called;
  std::vector<std::unique_ptr<ExpiringToken>> tokens;

  token_manager_.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (int i = 0; i < token_count; i++) {
    tokens.emplace_back(std::make_unique<ExpiringToken>(token_manager_.CreateToken()));
    EXPECT_FALSE(token_manager_.IsDiscardable());
    EXPECT_FALSE(on_discardable_called);
  }
  // Destroy the tokens in random order; the TokenManager will stay
  // not-empty until all the tokens have been destroyed.
  std::shuffle(tokens.begin(), tokens.end(), bit_generator);
  while (tokens.size() > 1) {
    tokens.pop_back();
    EXPECT_FALSE(token_manager_.IsDiscardable());
    EXPECT_FALSE(on_discardable_called);
  }
  tokens.pop_back();
  EXPECT_TRUE(token_manager_.IsDiscardable());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(TokenManagerTest, DestroyedWhileTokensOutstanding) {
  FakeDiskCleanupManager fake_disk_cleanup_manager;
  bool on_discardable_called;

  std::unique_ptr<TokenManager> token_manager = std::make_unique<TokenManager>();
  token_manager->SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  auto first_expiring_token = token_manager->CreateToken();
  auto second_expiring_token = token_manager->CreateToken();
  token_manager.reset();

  EXPECT_FALSE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
