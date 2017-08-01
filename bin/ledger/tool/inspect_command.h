// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TOOL_INSPECT_COMMAND_H_
#define APPS_LEDGER_SRC_TOOL_INSPECT_COMMAND_H_

#include <memory>

#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/coroutine/coroutine_impl.h"
#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/tool/command.h"
#include "lib/ftl/strings/string_view.h"

namespace tool {

// Command that cleans the local and remote storage of Ledger.
class InspectCommand : public Command {
 public:
  InspectCommand(std::vector<std::string> args,
                 const cloud_sync::UserConfig& user_config,
                 ftl::StringView user_repository_path);
  ~InspectCommand() override {}

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  void ListPages(ftl::Closure on_done);

  void DisplayCommit(ftl::Closure on_done);
  void PrintCommit(std::unique_ptr<const storage::Commit> commit,
                   ftl::Closure on_done);

  void DisplayCommitGraph(ftl::Closure on_done);
  void DisplayGraphCoroutine(coroutine::CoroutineHandler* handler,
                             storage::PageId page_id,
                             ftl::Closure on_done);

  void PrintHelp(ftl::Closure on_done);

  std::unique_ptr<storage::LedgerStorageImpl> GetLedgerStorage();

  std::unique_ptr<storage::PageStorage> storage_;
  const std::vector<std::string> args_;
  const std::string app_id_;
  const std::string user_repository_path_;
  coroutine::CoroutineServiceImpl coroutine_service_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InspectCommand);
};

}  // namespace tool

#endif  // APPS_LEDGER_SRC_TOOL_INSPECT_COMMAND_H_
