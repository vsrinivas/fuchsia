// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_CLIENT_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

class ClientApp : public CommitWatcher {
 public:
  ClientApp(ftl::CommandLine command_line);

 private:
  // CommitWatcher:
  void OnRemoteCommit(Commit commit, std::string timestamp) override;

  void OnConnectionError() override;

  void OnMalformedNotification() override;

  void Start();

  void CheckObjects();

  void CheckGetObject(std::string id, std::string content);

  void CheckCommits();

  void CheckGetCommits(Commit commit);

  void CheckWatchExistingCommits(Commit expected_commit);

  void CheckWatchNewCommits();

  void Done();

  ftl::CommandLine command_line_;
  std::unique_ptr<modular::ApplicationContext> context_;
  std::unique_ptr<ledger::NetworkService> network_service_;
  std::unique_ptr<firebase::Firebase> firebase_;
  std::unique_ptr<CloudProvider> cloud_provider_;
  std::function<void(Commit, std::string)> on_remote_commit_;
  std::function<void(ftl::StringView)> on_error_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ClientApp);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_CLIENT_CLIENT_H_
