// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_WATCHER_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_WATCHER_H_

#include "apps/ledger/src/cloud_provider/public/commit.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

class CommitWatcher {
 public:
  CommitWatcher() {}
  virtual ~CommitWatcher() {}

  // Called when a new commit is added to the cloud. |timestamp| is opaque
  // to the client, but can be passed back to CloudProvider as a query
  // parameter.
  virtual void OnRemoteCommit(Commit commit, std::string timestamp) = 0;

  // Called upon failure to establish a network connection, or when such
  // connection breaks. No further commit notifications are delivered after this
  // call is received.
  virtual void OnConnectionError() = 0;

  // Called when the watcher fails to decode the received notification. No
  // further commit notifications are delivered after this call is received.
  virtual void OnMalformedNotification() = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitWatcher);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_WATCHER_H_
