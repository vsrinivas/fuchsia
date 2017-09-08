// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/cloud_provider_impl.h"

#include "apps/ledger/src/convert/convert.h"
#include "garnet/public/lib/ftl/logging.h"

namespace cloud_provider_firebase {

CloudProviderImpl::CloudProviderImpl(
    ftl::RefPtr<ftl::TaskRunner> main_runner,
    std::string user_id,
    ConfigPtr config,
    std::unique_ptr<auth_provider::AuthProvider> auth_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : main_runner_(std::move(main_runner)),
      user_id_(user_id),
      auth_provider_(std::move(auth_provider)),
      binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

CloudProviderImpl::~CloudProviderImpl() {}

void CloudProviderImpl::GetDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
    const GetDeviceSetCallback& callback) {
  device_sets_.emplace(auth_provider_.get(), std::move(device_set));
  callback(cloud_provider::Status::OK);
}

void CloudProviderImpl::GetPageCloud(
    fidl::Array<uint8_t> /*app_id*/,
    fidl::Array<uint8_t> /*page_id*/,
    fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
    const GetPageCloudCallback& callback) {
  page_clouds_.emplace(auth_provider_.get(), std::move(page_cloud));
  callback(cloud_provider::Status::OK);
}

}  // namespace cloud_provider_firebase
