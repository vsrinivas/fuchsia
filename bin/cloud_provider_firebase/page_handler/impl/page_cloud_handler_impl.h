// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PAGE_CLOUD_HANDLER_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PAGE_CLOUD_HANDLER_IMPL_H_

#include <lib/zx/socket.h>

#include <map>
#include <memory>
#include <string>

#include "lib/fsl/vmo/sized_vmo.h"
#include "peridot/bin/cloud_provider_firebase/gcs/cloud_storage.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/watch_client_impl.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/page_cloud_handler.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/public/types.h"
#include "peridot/lib/firebase/firebase.h"
#include "peridot/lib/firebase/watch_client.h"

namespace cloud_provider_firebase {

class PageCloudHandlerImpl : public PageCloudHandler {
 public:
  PageCloudHandlerImpl(firebase::Firebase* firebase,
                       gcs::CloudStorage* cloud_storage);
  ~PageCloudHandlerImpl() override;

  // PageCloudHandler:
  void AddCommits(const std::string& auth_token, std::vector<Commit> commits,
                  const std::function<void(Status)>& callback) override;

  void WatchCommits(const std::string& auth_token,
                    const std::string& min_timestamp,
                    CommitWatcher* watcher) override;

  void UnwatchCommits(CommitWatcher* watcher) override;

  void GetCommits(
      const std::string& auth_token, const std::string& min_timestamp,
      std::function<void(Status, std::vector<Record>)> callback) override;

  void AddObject(const std::string& auth_token, ObjectDigestView object_digest,
                 fsl::SizedVmo data,
                 std::function<void(Status)> callback) override;

  void GetObject(
      const std::string& auth_token, ObjectDigestView object_digest,
      std::function<void(Status status, uint64_t size, zx::socket data)>
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

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_IMPL_PAGE_CLOUD_HANDLER_IMPL_H_
