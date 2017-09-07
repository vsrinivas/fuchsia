// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/page_cloud_impl.h"

namespace cloud_provider_firebase {

PageCloudImpl::PageCloudImpl(
    auth_provider::AuthProvider* auth_provider,
    fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : auth_provider_(auth_provider), binding_(this, std::move(request)) {
  FTL_DCHECK(auth_provider_);
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

PageCloudImpl::~PageCloudImpl() {}

void PageCloudImpl::AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                               const AddCommitsCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void PageCloudImpl::GetCommits(fidl::Array<uint8_t> min_position_token,
                               const GetCommitsCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
}

void PageCloudImpl::AddObject(fidl::Array<uint8_t> id,
                              mx::vmo data,
                              const AddObjectCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void PageCloudImpl::GetObject(fidl::Array<uint8_t> id,
                              const GetObjectCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR, 0u, mx::socket());
}

void PageCloudImpl::SetWatcher(
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
    fidl::Array<uint8_t> min_position_token,
    const SetWatcherCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firebase
