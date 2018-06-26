// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_TESTING_PAGE_CLOUD_HANDLER_EMPTY_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_TESTING_PAGE_CLOUD_HANDLER_EMPTY_IMPL_H_

#include <functional>
#include <string>
#include <vector>

#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include "peridot/bin/cloud_provider_firebase/page_handler/public/page_cloud_handler.h"

namespace cloud_provider_firebase {

// Empty implementation of PageCloudHandler.  All methods do nothing and return
// dummy or empty responses.
class PageCloudHandlerEmptyImpl : public PageCloudHandler {
 public:
  PageCloudHandlerEmptyImpl() = default;
  ~PageCloudHandlerEmptyImpl() override = default;

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
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_PAGE_HANDLER_TESTING_PAGE_CLOUD_HANDLER_EMPTY_IMPL_H_
