// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/page_handler/testing/page_cloud_handler_empty_impl.h"

#include "lib/fxl/logging.h"

namespace cloud_provider_firebase {
void PageCloudHandlerEmptyImpl::AddCommits(
    const std::string& /*auth_token*/, std::vector<Commit> /*commits*/,
    const std::function<void(Status)>& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void PageCloudHandlerEmptyImpl::WatchCommits(
    const std::string& /*auth_token*/, const std::string& /*min_timestamp*/,
    CommitWatcher* /*watcher*/) {
  FXL_NOTIMPLEMENTED();
}

void PageCloudHandlerEmptyImpl::UnwatchCommits(CommitWatcher* /*watcher*/) {
  FXL_NOTIMPLEMENTED();
}

void PageCloudHandlerEmptyImpl::GetCommits(
    const std::string& /*auth_token*/, const std::string& /*min_timestamp*/,
    std::function<void(Status, std::vector<Record>)> /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void PageCloudHandlerEmptyImpl::AddObject(
    const std::string& /*auth_token*/, ObjectDigestView /*object_digest*/,
    fsl::SizedVmo /*data*/, std::function<void(Status)> /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void PageCloudHandlerEmptyImpl::GetObject(
    const std::string& /*auth_token*/, ObjectDigestView /*object_digest*/,
    std::function<void(Status status, uint64_t size, zx::socket data)>
    /*callback*/) {
  FXL_NOTIMPLEMENTED();
}
}  // namespace cloud_provider_firebase
