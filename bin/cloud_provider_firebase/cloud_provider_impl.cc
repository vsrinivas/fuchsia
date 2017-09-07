// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/cloud_provider_impl.h"

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "apps/ledger/src/convert/convert.h"
#include "garnet/public/lib/ftl/logging.h"

namespace cloud_provider_firebase {

CloudProviderImpl::CloudProviderImpl(
    ftl::RefPtr<ftl::TaskRunner> main_runner,
    ConfigPtr config,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : main_runner_(std::move(main_runner)), binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });

  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));
  // The class shuts down when the connection to the token provider is lost.
  token_provider_ptr.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "Lost connection to TokenProvider, "
                   << "shutting down the cloud provider.";
    if (on_empty_) {
      on_empty_();
    }
  });

  auth_provider_ = std::make_unique<auth_provider::AuthProviderImpl>(
      main_runner_, config->api_key, std::move(token_provider_ptr),
      std::make_unique<backoff::ExponentialBackoff>());
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
    fidl::InterfaceRequest<cloud_provider::PageCloud> /*page_cloud*/,
    const GetPageCloudCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firebase
