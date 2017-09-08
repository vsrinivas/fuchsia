// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/ledger/src/cloud_provider/impl/watch_client_impl.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/firebase/watch_client.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "mx/socket.h"
#include "mx/vmo.h"

namespace cloud_provider_firebase {

class CloudProviderImpl : public CloudProvider {
 public:
  CloudProviderImpl(firebase::Firebase* firebase,
                    gcs::CloudStorage* cloud_storage);
  ~CloudProviderImpl() override;

  // CloudProvider:
  void AddCommits(const std::string& auth_token,
                  std::vector<Commit> commits,
                  const std::function<void(Status)>& callback) override;

  void WatchCommits(const std::string& auth_token,
                    const std::string& min_timestamp,
                    CommitWatcher* watcher) override;

  void UnwatchCommits(CommitWatcher* watcher) override;

  void GetCommits(
      const std::string& auth_token,
      const std::string& min_timestamp,
      std::function<void(Status, std::vector<Record>)> callback) override;

  void AddObject(const std::string& auth_token,
                 ObjectIdView object_id,
                 mx::vmo data,
                 std::function<void(Status)> callback) override;

  void GetObject(
      const std::string& auth_token,
      ObjectIdView object_id,
      std::function<void(Status status, uint64_t size, mx::socket data)>
          callback) override;

 private:
  // Returns the Firebase query params.
  //
  // If |min_timestamp| is not empty, the resulting query params filter the
  // commits so that only commits not older than |min_timestamp| are returned.
  std::vector<std::string> GetQueryParams(const std::string& auth_token,
                                          const std::string& min_timestamp);

  firebase::Firebase* const firebase_;
  gcs::CloudStorage* const cloud_storage_;
  std::map<CommitWatcher*, std::unique_ptr<WatchClientImpl>> watchers_;
};

}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_IMPL_CLOUD_PROVIDER_IMPL_H_
