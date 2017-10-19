// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/app/erase_remote_repository_operation.h"
#include "peridot/bin/ledger/app/ledger_repository_impl.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/callback/managed_container.h"
#include "peridot/bin/ledger/cloud_sync/public/user_config.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"

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

  LedgerRepositoryFactoryImpl(Delegate* delegate,
                              ledger::Environment* environment);
  ~LedgerRepositoryFactoryImpl() override;

 private:
  class LedgerRepositoryContainer;
  struct RepositoryInformation;

  // LedgerRepositoryFactory:
  void GetRepository(
      const fidl::String& repository_path,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      fidl::InterfaceRequest<LedgerRepository> repository_request,
      const GetRepositoryCallback& callback) override;
  void EraseRepository(
      const fidl::String& repository_path,
      fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
      const EraseRepositoryCallback& callback) override;

  void CreateRepository(LedgerRepositoryContainer* container,
                        const RepositoryInformation& repository_information,
                        cloud_sync::UserConfig user_config);

  void OnVersionMismatch(RepositoryInformation repository_information);

  Status DeleteRepositoryDirectory(
      const RepositoryInformation& repository_information);

  Delegate* const delegate_;
  ledger::Environment* const environment_;

  callback::AutoCleanableMap<std::string, LedgerRepositoryContainer>
      repositories_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
