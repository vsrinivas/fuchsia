// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_provider/fake_cloud_provider.h"

#include "peridot/bin/ledger/convert/convert.h"

namespace ledger {

FakeCloudProvider::FakeCloudProvider() {}

FakeCloudProvider::~FakeCloudProvider() {}

void FakeCloudProvider::GetDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
    const GetDeviceSetCallback& callback) {
  device_sets_.emplace(std::move(device_set));
  callback(cloud_provider::Status::OK);
}

void FakeCloudProvider::GetPageCloud(
    fidl::Array<uint8_t> app_id,
    fidl::Array<uint8_t> page_id,
    fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
    const GetPageCloudCallback& callback) {
  const std::string key =
      convert::ToString(app_id) + "_" + convert::ToString(page_id);
  auto it = page_clouds_.find(key);
  if (it != page_clouds_.end()) {
    it->second.Bind(std::move(page_cloud));
    callback(cloud_provider::Status::OK);
    return;
  }

  auto ret =
      page_clouds_.emplace(std::piecewise_construct, std::forward_as_tuple(key),
                           std::forward_as_tuple());
  ret.first->second.Bind(std::move(page_cloud));
  callback(cloud_provider::Status::OK);
}

void FakeCloudProvider::EraseAllData(const EraseAllDataCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace ledger
