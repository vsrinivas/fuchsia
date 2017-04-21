// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLEAN_COMMAND_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLEAN_COMMAND_H_

#include <memory>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_sync/client/command.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/network/network_service.h"

namespace cloud_sync {

// Command that cleans the local and remote storage of Ledger.
class CleanCommand : public Command {
 public:
  CleanCommand(const configuration::Configuration& configuration,
               ledger::NetworkService* network_service);
  ~CleanCommand() {}

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  const configuration::Configuration& configuration_;
  std::unique_ptr<firebase::Firebase> firebase_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CleanCommand);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLEAN_COMMAND_H_
