// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/testing/test_cloud_provider.h"

#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"

namespace cloud_sync {

TestCloudProvider::TestCloudProvider(fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : binding_(this, std::move(request)), device_set_binding_(&device_set) {}

TestCloudProvider::~TestCloudProvider() = default;

void TestCloudProvider::GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> request,
                                     GetDeviceSetCallback callback) {
  device_set_binding_.Bind(std::move(request));
  callback(cloud_provider::Status::OK);
}

void TestCloudProvider::GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                                     fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
                                     GetPageCloudCallback callback) {
  page_cloud_[std::make_pair(convert::ToString(app_id), convert::ToString(page_id))] =
      std::make_unique<TestPageCloud>(std::move(page_cloud));
  page_ids_requested.push_back(convert::ToString(page_id));
  callback(cloud_provider::Status::OK);
}

}  // namespace cloud_sync
