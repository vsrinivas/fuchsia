// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/test/cloud_provider_empty_impl.h"

#include "lib/ftl/logging.h"

namespace cloud_provider_firebase {
namespace test {

void CloudProviderEmptyImpl::AddCommits(
    const std::string& /*auth_token*/,
    std::vector<Commit> /*commits*/,
    const std::function<void(Status)>& /*callback*/) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderEmptyImpl::WatchCommits(const std::string& /*auth_token*/,
                                          const std::string& /*min_timestamp*/,
                                          CommitWatcher* /*watcher*/) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderEmptyImpl::UnwatchCommits(CommitWatcher* /*watcher*/) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderEmptyImpl::GetCommits(
    const std::string& /*auth_token*/,
    const std::string& /*min_timestamp*/,
    std::function<void(Status, std::vector<Record>)> /*callback*/) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderEmptyImpl::AddObject(
    const std::string& /*auth_token*/,
    ObjectIdView /*object_id*/,
    mx::vmo /*data*/,
    std::function<void(Status)> /*callback*/) {
  FTL_NOTIMPLEMENTED();
}

void CloudProviderEmptyImpl::GetObject(
    const std::string& /*auth_token*/,
    ObjectIdView /*object_id*/,
    std::function<void(Status status, uint64_t size, mx::socket data)>
    /*callback*/) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace test
}  // namespace cloud_provider_firebase
