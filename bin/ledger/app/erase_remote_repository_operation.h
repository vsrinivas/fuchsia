// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
#define APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_

#include <memory>
#include <string>

#include "peridot/bin/ledger/auth_provider/auth_provider.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/firebase/firebase_impl.h"
#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace ledger {

// Asynchronous operation that erases data in the cloud for the given
// repository.
class EraseRemoteRepositoryOperation {
 public:
  EraseRemoteRepositoryOperation(
      fxl::RefPtr<fxl::TaskRunner> task_runner,
      ledger::NetworkService* network_service,
      std::string server_id,
      std::string api_key,
      modular::auth::TokenProviderPtr token_provider);
  ~EraseRemoteRepositoryOperation();

  EraseRemoteRepositoryOperation(EraseRemoteRepositoryOperation&& other);
  EraseRemoteRepositoryOperation& operator=(
      EraseRemoteRepositoryOperation&& other);

  void Start(std::function<void(bool)> on_done);

 private:
  void ClearDeviceMap();

  void EraseRemote();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  ledger::NetworkService* network_service_;
  std::string repository_path_;
  std::string server_id_;
  std::string api_key_;
  std::unique_ptr<auth_provider::AuthProvider> auth_provider_;

  std::function<void(bool)> on_done_;
  std::string user_id_;
  std::string auth_token_;
  std::unique_ptr<firebase::FirebaseImpl> firebase_;

  // Pending auth provider requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_provider_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EraseRemoteRepositoryOperation);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
