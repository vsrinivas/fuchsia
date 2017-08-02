// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/ledger_repository_for_testing.h"

#include "apps/modular/lib/fidl/app_client.h"
#include "apps/modular/lib/ledger/constants.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/test_runner/lib/application_context.h"

namespace modular {

// Template specializations for fidl services that don't have a Terminate()
// method.
template <>
void AppClient<ledger::LedgerRepositoryFactory>::ServiceTerminate(
    const std::function<void()>& done) {
  service_.set_connection_error_handler(done);
}

namespace testing {

// static
LedgerRepositoryForTesting* LedgerRepositoryForTesting::GetSingleton(
    const std::string& repository_path) {
  static std::unique_ptr<LedgerRepositoryForTesting> instance;
  if (!instance) {
    instance = std::make_unique<LedgerRepositoryForTesting>(repository_path);
  }
  return instance.get();
}

ledger::LedgerRepository* LedgerRepositoryForTesting::ledger_repository() {
  if (!ledger_repo_) {
    ledger_app_client_->primary_service()->GetRepository(
        repository_path_, nullptr, nullptr, ledger_repo_.NewRequest(),
        [](ledger::Status status) { FTL_CHECK(status == ledger::Status::OK); });
  }
  return ledger_repo_.get();
}

LedgerRepositoryForTesting::LedgerRepositoryForTesting(
    const std::string& repository_path)
    : repository_path_(repository_path) {
  FTL_LOG(INFO) << "New ledger repo instance";
  AppConfigPtr ledger_config = AppConfig::New();
  ledger_config->url = kLedgerAppUrl;
  ledger_config->args = fidl::Array<fidl::String>::New(1);
  ledger_config->args[0] = kLedgerNoMinfsWaitFlag;

  auto& app_launcher = test_runner::GetApplicationContext()->launcher();
  ledger_app_client_ =
      std::make_unique<AppClient<ledger::LedgerRepositoryFactory>>(
          app_launcher.get(), std::move(ledger_config));

  ledger_app_client_->primary_service()->GetRepository(
      repository_path_, nullptr, nullptr, ledger_repo_.NewRequest(),
      [](ledger::Status status) { FTL_CHECK(status == ledger::Status::OK); });
}

void LedgerRepositoryForTesting::Reset(ftl::Closure done) {
  if (ledger_repo_) {
    // It seems to take an entire second to erase a repository so for now,
    // we fire-and-forget the erase and report |done| right away.
    ledger_app_client_->primary_service()->EraseRepository(
        repository_path_, nullptr, nullptr, [](ledger::Status status) {});
    ledger_repo_.reset();
    done();
  }
}

}  // namespace testing
}  // namespace modular
