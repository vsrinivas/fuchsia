// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/ledger_repository_for_testing.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <zircon/status.h>

#include <utility>

#include "src/lib/fsl/io/fd.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/ledger_client/constants.h"

namespace modular_testing {

LedgerRepositoryForTesting::LedgerRepositoryForTesting()
    : component_context_(sys::ComponentContext::Create()), weak_ptr_factory_(this) {
  fuchsia::modular::AppConfig ledger_config;
  ledger_config.url = modular::kLedgerAppUrl;

  auto launcher = component_context_->svc()->Connect<fuchsia::sys::Launcher>();
  ledger_app_client_ =
      std::make_unique<modular::AppClient<fuchsia::ledger::internal::LedgerController>>(
          launcher.get(), std::move(ledger_config));

  ledger_repo_factory_.set_error_handler([](zx_status_t status) {
    FXL_CHECK(false) << "LedgerRepositoryFactory returned an error. Status: "
                     << zx_status_get_string(status);
  });
  ledger_app_client_->services().ConnectToService(ledger_repo_factory_.NewRequest());
}

LedgerRepositoryForTesting::~LedgerRepositoryForTesting() = default;

fuchsia::ledger::internal::LedgerRepository* LedgerRepositoryForTesting::ledger_repository() {
  if (!ledger_repo_) {
    ledger_repo_factory_->GetRepository(fsl::CloneChannelFromFileDescriptor(tmp_fs_.root_fd()),
                                        nullptr, "", ledger_repo_.NewRequest());
  }

  return ledger_repo_.get();
}

void LedgerRepositoryForTesting::Terminate(fit::function<void()> callback) {
  if (ledger_app_client_) {
    ledger_app_client_->Teardown(
        modular::kBasicTimeout,
        [this, weak_this = weak_ptr_factory_.GetWeakPtr(), callback = std::move(callback)] {
          ledger_repo_factory_.Unbind();
          callback();
          if (weak_this) {
            weak_this->ledger_app_client_.reset();
          }
        });
  } else {
    callback();
  }
}

}  // namespace modular_testing
