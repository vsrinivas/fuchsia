// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_CLOUD_PROVIDER_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_CLOUD_PROVIDER_EMPTY_IMPL_H_

#include <functional>
#include <string>
#include <vector>

#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "zx/socket.h"
#include "zx/vmo.h"

namespace cloud_provider_firebase {
namespace test {

// Empty implementation of CloudProvider.  All methods do nothing and return
// dummy or empty responses.
class CloudProviderEmptyImpl : public CloudProvider {
 public:
  CloudProviderEmptyImpl() = default;
  ~CloudProviderEmptyImpl() override = default;

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
                 zx::vmo data,
                 std::function<void(Status)> callback) override;

  void GetObject(
      const std::string& auth_token,
      ObjectIdView object_id,
      std::function<void(Status status, uint64_t size, zx::socket data)>
          callback) override;
};

}  // namespace test
}  // namespace cloud_provider_firebase

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_TEST_CLOUD_PROVIDER_EMPTY_IMPL_H_
