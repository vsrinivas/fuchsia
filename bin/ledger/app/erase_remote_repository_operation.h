// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
#define APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_

#include <memory>
#include <string>

#include "apps/ledger/src/cloud_sync/public/auth_provider.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

// Asynchronous operation that erases data in the cloud for the given
// repository.
class EraseRemoteRepositoryOperation {
 public:
  EraseRemoteRepositoryOperation(
      ftl::RefPtr<ftl::TaskRunner> task_runner,
      ledger::NetworkService* network_service,
      std::string server_id,
      std::string api_key,
      modular::auth::TokenProviderPtr token_provider);

  EraseRemoteRepositoryOperation(EraseRemoteRepositoryOperation&& other);
  EraseRemoteRepositoryOperation& operator=(
      EraseRemoteRepositoryOperation&& other);

  void Start(std::function<void(bool)> on_done);

 private:
  void EraseRemote();

  ledger::NetworkService* network_service_;
  std::string repository_path_;
  std::string server_id_;
  std::string api_key_;
  std::unique_ptr<cloud_sync::AuthProvider> auth_provider_;

  std::function<void(bool)> on_done_;
  std::string user_id_;
  std::string auth_token_;
  std::unique_ptr<firebase::FirebaseImpl> firebase_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EraseRemoteRepositoryOperation);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_ERASE_REMOTE_REPOSITORY_OPERATION_H_
