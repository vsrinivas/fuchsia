// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TOOL_CLEAN_COMMAND_H_
#define APPS_LEDGER_SRC_TOOL_CLEAN_COMMAND_H_

#include <memory>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/network/network_service.h"
#include "apps/ledger/src/tool/command.h"
#include "lib/ftl/strings/string_view.h"

namespace tool {

// Command that cleans the local and remote storage of Ledger.
class CleanCommand : public Command {
 public:
  CleanCommand(const cloud_sync::UserConfig& user_config,
               ftl::StringView user_repository_path,
               ledger::NetworkService* network_service);
  ~CleanCommand() {}

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  std::unique_ptr<firebase::FirebaseImpl> firebase_;
  const std::string user_repository_path_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CleanCommand);
};

}  // namespace tool

#endif  // APPS_LEDGER_SRC_TOOL_CLEAN_COMMAND_H_
