// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_COMMIT_WATCHER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_COMMIT_WATCHER_H_

#include <vector>

#include <lib/fxl/macros.h>

#include "peridot/bin/cloud_provider_firebase/page_handler/public/commit.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/record.h"

namespace cloud_provider_firebase {

class CommitWatcher {
 public:
  CommitWatcher() {}
  virtual ~CommitWatcher() {}

  // Called when a new batch of commits is added to the cloud.
  virtual void OnRemoteCommits(std::vector<Record> records) = 0;

  // Called upon failure to establish a network connection, or when such
  // connection breaks. No further commit notifications are delivered after this
  // call is received.
  virtual void OnConnectionError() = 0;

  // Called when the remote watcher is interrupted because the token is no
  // longer valid. No further commit notifications are delivered after this
  // call is received.
  virtual void OnTokenExpired() = 0;

  // Called when the watcher fails to decode the received notification. No
  // further commit notifications are delivered after this call is received.
  virtual void OnMalformedNotification() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CommitWatcher);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_PUBLIC_COMMIT_WATCHER_H_
