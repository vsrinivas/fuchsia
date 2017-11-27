// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/cloud_provider_impl.h"

#include <utility>

#include "lib/fxl/logging.h"

namespace cloud_provider_firestore {

CloudProviderImpl::CloudProviderImpl(
    std::string user_id,
    std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : user_id_(std::move(user_id)),
      firebase_auth_(std::move(firebase_auth)),
      binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
  // The class also shuts down when the auth provider is disconnected.
  firebase_auth_->set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the token provider, "
                   << "shutting down the cloud provider.";
    if (on_empty_) {
      on_empty_();
    }
  });
}

CloudProviderImpl::~CloudProviderImpl() {}

void CloudProviderImpl::GetDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> /*device_set*/,
    const GetDeviceSetCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void CloudProviderImpl::GetPageCloud(
    fidl::Array<uint8_t> /*app_id*/,
    fidl::Array<uint8_t> /*page_id*/,
    fidl::InterfaceRequest<cloud_provider::PageCloud> /*page_cloud*/,
    const GetPageCloudCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void CloudProviderImpl::EraseAllData(const EraseAllDataCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firestore
