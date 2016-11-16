// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_

#include <functional>
#include <string>
#include <vector>

#include "apps/ledger/src/cloud_provider/public/commit.h"
#include "apps/ledger/src/cloud_provider/public/commit_watcher.h"
#include "apps/ledger/src/cloud_provider/public/record.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "lib/ftl/macros.h"
#include "mx/datapipe.h"
#include "mx/vmo.h"

namespace cloud_provider {

// This API captures Ledger requirements for a cloud sync provider.
//
// A CloudProvider instance is scoped to a particular page.
//
// When delivered from the server, commits come along with timestamps.
// These timestamps are server timestamps , i.e. they represent the time of
// registering the commit on the server. Their meaning is opaque to the client
// and depends on the particular service provider, but they can be used to make
// scoped queries - see GetCommits(), WatchCommits().
class CloudProvider {
 public:
  CloudProvider() {}
  virtual ~CloudProvider() {}

  // Adds the given commit to the cloud. The given callback will be called
  // asynchronously with Status::OK if the operation have succeeded.
  virtual void AddCommit(const Commit& commit,
                         const std::function<void(Status)>& callback) = 0;

  // Registers the given watcher to be notified about commits already present
  // and these being added to the cloud later. This includes commits added by
  // the same CloudProvider instance through AddCommit().
  //
  // |watcher| is firstly notified about all commits already present in the
  // cloud. Then, it is notified about new commits as they are registered. This
  // allows the client to avoid the race condition when a commit is registered
  // in the cloud between pulling down a list of commits and establishing a
  // watcher for a new one.
  //
  // Only commits not older than |min_timestamp| are passed to the |watcher|.
  // Passing empty |min_timestamp| covers all commits.
  //
  // Each |watcher| object can be registered only once at a time.
  virtual void WatchCommits(const std::string& min_timestamp,
                            CommitWatcher* watcher) = 0;

  // Unregisters the given watcher. No methods on the watcher will be called
  // after this returns.
  virtual void UnwatchCommits(CommitWatcher* watcher) = 0;

  // Retrieves commits not older than the given |min_timestamp|.  Passing empty
  // |min_timestamp| retrieves all commits.
  //
  // Result is a vector of pairs of the retrieved commits and their
  // corresponding server timestamps.
  virtual void GetCommits(
      const std::string& min_timestamp,
      std::function<void(Status, std::vector<Record>&&)> callback) = 0;

  // Uploads the given object to the cloud under the given id.
  virtual void AddObject(ObjectIdView object_id,
                         mx::vmo data,
                         std::function<void(Status)> callback) = 0;

  // Retrieves the object of the given id from the cloud. The size of the object
  // is passed to the callback along with the data pipe handle, so that the
  // client can verify that all data was streamed when draining the pipe.
  virtual void GetObject(
      ObjectIdView object_id,
      std::function<void(Status status,
                         uint64_t size,
                         mx::datapipe_consumer data)> callback) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CloudProvider);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_CLOUD_PROVIDER_H_
