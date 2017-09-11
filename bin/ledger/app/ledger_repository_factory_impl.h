// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/src/app/erase_remote_repository_operation.h"
#include "apps/ledger/src/app/ledger_repository_impl.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/callback/cancellable.h"
#include "apps/ledger/src/callback/pending_operation.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/fxl/macros.h"

namespace ledger {

class LedgerRepositoryFactoryImpl : public LedgerRepositoryFactory {
 public:
  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    virtual void EraseRepository(
        EraseRemoteRepositoryOperation erase_remote_repository_operation,
        std::function<void(bool)> callback) = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  enum class ConfigPersistence { PERSIST, FORGET };
  explicit LedgerRepositoryFactoryImpl(Delegate* delegate,
                                       ledger::Environment* environment,
                                       ConfigPersistence config_persistence);
  ~LedgerRepositoryFactoryImpl() override;

 private:
  class LedgerRepositoryContainer;
  struct RepositoryInformation;

  // LedgerRepositoryFactory:
  void GetRepository(
      const fidl::String& repository_path,
      FirebaseConfigPtr firebase_config,
      fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
      fidl::InterfaceRequest<LedgerRepository> repository_request,
      const GetRepositoryCallback& callback) override;
  void EraseRepository(
      const fidl::String& repository_path,
      FirebaseConfigPtr firebase_config,
      fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
      const EraseRepositoryCallback& callback) override;

  // Verifies that the current server id is not different from the server id
  // used in a previous run and wipes the local state in case of a mismatch.
  //
  // Ledger does not support cloud migrations - once the repository is synced
  // with a cloud, we can't change the server.
  bool CheckSyncConfig(const cloud_sync::UserConfig& user_config,
                       const RepositoryInformation& repository_information);

  void CreateRepository(LedgerRepositoryContainer* container,
                        const RepositoryInformation& repository_information,
                        cloud_sync::UserConfig user_config);

  void OnVersionMismatch(RepositoryInformation repository_information);

  Status DeleteRepositoryDirectory(
      const RepositoryInformation& repository_information);

  Delegate* const delegate_;
  ledger::Environment* const environment_;
  const ConfigPersistence config_persistence_;

  callback::AutoCleanableMap<std::string, LedgerRepositoryContainer>
      repositories_;

  // Pending auth provider requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_provider_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
