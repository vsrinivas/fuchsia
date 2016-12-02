// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_DOCTOR_COMMAND_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_DOCTOR_COMMAND_H_

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_sync/client/command.h"
#include "apps/ledger/src/network/network_service.h"

namespace cloud_sync {

// Command that runs a series of check-ups for the sync configuration.
class DoctorCommand : public Command, public cloud_provider::CommitWatcher {
 public:
  DoctorCommand(ledger::NetworkService* network_service,
                const std::string& firebase_id,
                cloud_provider::CloudProvider* cloud_provider);
  ~DoctorCommand();

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  // cloud_provider::CommitWatcher:
  void OnRemoteCommit(cloud_provider::Commit commit,
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

  ledger::NetworkService* const network_service_;
  std::string firebase_id_;
  cloud_provider::CloudProvider* const cloud_provider_;
  ftl::Closure on_done_;
  std::function<void(cloud_provider::Commit, std::string)> on_remote_commit_;
  std::function<void(ftl::StringView)> on_error_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DoctorCommand);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_DOCTOR_COMMAND_H_
