// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/test/integration/integration_test.h"
#include "apps/ledger/src/test/integration/test_utils.h"
#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace test {
namespace integration {
namespace {

class LedgerRepositoryIntegrationTest : public IntegrationTest {
 public:
  LedgerRepositoryIntegrationTest() {}
  ~LedgerRepositoryIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryIntegrationTest);
};

class EmptyTokenProvider : public modular::auth::TokenProvider {
 public:
  explicit EmptyTokenProvider(
      fidl::InterfaceRequest<modular::auth::TokenProvider> request)
      : binding_(this, std::move(request)) {
    error_ = modular::auth::AuthErr::New();
    error_->status = modular::auth::Status::OK;
    error_->message = "";
  }
  ~EmptyTokenProvider() override {}

 private:
  void GetAccessToken(const GetAccessTokenCallback& callback) override {
    callback(fidl::String(""), error_.Clone());
  }
  void GetIdToken(const GetIdTokenCallback& callback) override {
    callback(fidl::String(""), error_.Clone());
  }
  void GetFirebaseAuthToken(
      const fidl::String& /*firebase_api_key*/,
      const GetFirebaseAuthTokenCallback& callback) override {
    modular::auth::FirebaseTokenPtr token = modular::auth::FirebaseToken::New();
    token->id_token = "";
    token->local_id = "user_id";
    token->email = "";
    callback(std::move(token), error_.Clone());
  }
  void GetClientId(const GetClientIdCallback& callback) override {
    callback(fidl::String(""));
  }

  fidl::Binding<modular::auth::TokenProvider> binding_;
  modular::auth::AuthErrPtr error_;
  FXL_DISALLOW_COPY_AND_ASSIGN(EmptyTokenProvider);
};

// Verifies that the LedgerRepository and its children are shut down on token
// manager connection error.
TEST_F(LedgerRepositoryIntegrationTest, ShutDownOnTokenProviderError) {
  auto instance = NewLedgerAppInstance();
  const auto timeout = fxl::TimeDelta::FromSeconds(1);

  ledger::LedgerRepositoryPtr repository = instance->GetTestLedgerRepository();
  bool repository_disconnected = false;
  repository.set_connection_error_handler(
      [&repository_disconnected] { repository_disconnected = true; });

  ledger::LedgerPtr ledger = instance->GetTestLedger();
  bool ledger_disconnected = false;
  ledger.set_connection_error_handler(
      [&ledger_disconnected] { ledger_disconnected = true; });

  instance->UnbindTokenProvider();

  ASSERT_FALSE(ledger.WaitForIncomingResponseWithTimeout(timeout));
  EXPECT_TRUE(ledger_disconnected);

  ASSERT_FALSE(repository.WaitForIncomingResponseWithTimeout(timeout));
  EXPECT_TRUE(repository_disconnected);
}

}  // namespace
}  // namespace integration
}  // namespace test
