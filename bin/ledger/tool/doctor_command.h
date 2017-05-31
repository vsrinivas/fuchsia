// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TOOL_DOCTOR_COMMAND_H_
#define APPS_LEDGER_SRC_TOOL_DOCTOR_COMMAND_H_

#include <vector>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "apps/ledger/src/network/network_service.h"
#include "apps/ledger/src/tool/command.h"

namespace tool {

// Command that runs a series of check-ups for the sync configuration.
class DoctorCommand : public Command, public cloud_provider::CommitWatcher {
 public:
  DoctorCommand(cloud_sync::UserConfig* user_config,
                ledger::NetworkService* network_service);
  ~DoctorCommand();

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  // cloud_provider::CommitWatcher:
  void OnRemoteCommits(std::vector<cloud_provider::Commit> commit,
                       std::string timestamp) override;

  void OnConnectionError() override;

  void OnMalformedNotification() override;

  void CheckHttpConnectivity();

  void CheckHttpsConnectivity();

  void CheckObjects();

  void CheckGetObject(std::string id, std::string content);

  void CheckCommits();

  void CheckGetCommits(cloud_provider::Commit commit);

  void CheckGetCommitsByTimestamp(cloud_provider::Commit expected_commit,
                                  std::string timestamp);

  void CheckWatchExistingCommits(cloud_provider::Commit expected_commit);

  void CheckWatchNewCommits();

  void Done();

  cloud_sync::UserConfig* const user_config_;
  ledger::NetworkService* const network_service_;
  std::unique_ptr<firebase::Firebase> firebase_;
  std::unique_ptr<gcs::CloudStorage> cloud_storage_;
  std::unique_ptr<cloud_provider::CloudProvider> cloud_provider_;
  ftl::Closure on_done_;
  std::function<void(cloud_provider::Commit, std::string)> on_remote_commit_;
  std::function<void(ftl::StringView)> on_error_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DoctorCommand);
};

}  // namespace tool

#endif  // APPS_LEDGER_SRC_TOOL_DOCTOR_COMMAND_H_
