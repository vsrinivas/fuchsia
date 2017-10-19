// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/ledger_repository_for_testing.h"

#include <utility>

#include "lib/config/fidl/config.fidl.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/ledger_client/constants.h"

namespace modular {

namespace testing {

LedgerRepositoryForTesting::LedgerRepositoryForTesting(
    const std::string& repository_name)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      tmp_dir_("/tmp/" + repository_name) {
  AppConfigPtr ledger_config = AppConfig::New();
  ledger_config->url = kLedgerAppUrl;
  ledger_config->args = fidl::Array<fidl::String>::New(1);
  ledger_config->args[0] = kLedgerNoMinfsWaitFlag;

  auto& app_launcher = application_context_->launcher();
  ledger_app_client_ = std::make_unique<AppClient<ledger::LedgerController>>(
      app_launcher.get(), std::move(ledger_config));

  ConnectToService(ledger_app_client_->services(),
                   ledger_repo_factory_.NewRequest());
}

LedgerRepositoryForTesting::~LedgerRepositoryForTesting() = default;

ledger::LedgerRepository* LedgerRepositoryForTesting::ledger_repository() {
  if (!ledger_repo_) {
    ledger_repo_factory_->GetRepository(
        tmp_dir_.path(), nullptr, ledger_repo_.NewRequest(),
        [this](ledger::Status status) {
          FXL_CHECK(status == ledger::Status::OK);
        });
  }

  return ledger_repo_.get();
}

void LedgerRepositoryForTesting::Reset(std::function<void()> done) {
  if (ledger_repo_) {
    ledger_repo_factory_->EraseRepository(tmp_dir_.path(), nullptr,
                                          [this, done](ledger::Status status) {
                                            ledger_repo_.reset();
                                            done();
                                          });
  } else {
    done();
  }
}

void LedgerRepositoryForTesting::Terminate(std::function<void()> done) {
  if (ledger_app_client_) {
    ledger_app_client_->Teardown(kBasicTimeout, [this, done] {
      ledger_repo_factory_.reset();
      ledger_app_client_.reset();
      done();
    });

  } else {
    done();
  }
}

}  // namespace testing
}  // namespace modular
