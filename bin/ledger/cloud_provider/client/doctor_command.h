// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_DOCTOR_COMMAND_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_DOCTOR_COMMAND_H_

#include "apps/ledger/src/cloud_provider/client/command.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/network/network_service.h"

namespace cloud_provider {

// Command that runs a series of check-ups for the sync configuration.
class DoctorCommand : public Command, public CommitWatcher {
 public:
  DoctorCommand(ledger::NetworkService* network_service,
                CloudProvider* cloud_provider);
  ~DoctorCommand();

  // Command:
  void Start(ftl::Closure on_done) override;

 private:
  // CommitWatcher:
  void OnRemoteCommit(Commit commit, std::string timestamp) override;

  void OnConnectionError() override;

  void OnMalformedNotification() override;

  void CheckHttpConnectivity();

  void CheckHttpsConnectivity();

  void CheckObjects();

  void CheckGetObject(std::string id, std::string content);

  void CheckCommits();

  void CheckGetCommits(Commit commit);

  void CheckWatchExistingCommits(Commit expected_commit);

  void CheckWatchNewCommits();

  void Done();

  ledger::NetworkService* const network_service_;
  CloudProvider* const cloud_provider_;
  ftl::Closure on_done_;
  std::function<void(Commit, std::string)> on_remote_commit_;
  std::function<void(ftl::StringView)> on_error_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DoctorCommand);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_DOCTOR_COMMAND_H_
