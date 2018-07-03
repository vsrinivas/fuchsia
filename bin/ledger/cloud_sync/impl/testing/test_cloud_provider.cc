// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/testing/test_cloud_provider.h"

#include <lib/fxl/logging.h>

namespace cloud_sync {

TestCloudProvider::TestCloudProvider(
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : binding_(this, std::move(request)), device_set_binding_(&device_set) {}

TestCloudProvider::~TestCloudProvider() {}

void TestCloudProvider::GetDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request,
    GetDeviceSetCallback callback) {
  device_set_binding_.Bind(std::move(request));
  callback(cloud_provider::Status::OK);
}

void TestCloudProvider::GetPageCloud(
    fidl::VectorPtr<uint8_t> /*app_id*/, fidl::VectorPtr<uint8_t> /*page_id*/,
    fidl::InterfaceRequest<cloud_provider::PageCloud> /*page_cloud*/,
    GetPageCloudCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace cloud_sync
