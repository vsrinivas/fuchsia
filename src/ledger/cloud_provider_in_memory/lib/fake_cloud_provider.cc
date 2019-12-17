// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_in_memory/lib/fake_cloud_provider.h"

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/rng/random.h"

namespace ledger {

FakeCloudProvider::Builder::Builder(async_dispatcher_t* dispatcher, Random* random)
    : dispatcher_(dispatcher), random_(random) {}

FakeCloudProvider::Builder::~Builder() = default;

FakeCloudProvider::Builder& FakeCloudProvider::Builder::SetInjectNetworkError(
    InjectNetworkError inject_network_error) {
  inject_network_error_ = inject_network_error;
  return *this;
}

FakeCloudProvider::Builder& FakeCloudProvider::Builder::SetInjectMissingDiff(
    InjectMissingDiff inject_missing_diff) {
  inject_missing_diff_ = inject_missing_diff;
  return *this;
}

FakeCloudProvider::Builder& FakeCloudProvider::Builder::SetCloudEraseOnCheck(
    CloudEraseOnCheck cloud_erase_on_check) {
  cloud_erase_on_check_ = cloud_erase_on_check;
  return *this;
}

FakeCloudProvider::Builder& FakeCloudProvider::Builder::SetCloudEraseFromWatcher(
    CloudEraseFromWatcher cloud_erase_from_watcher) {
  cloud_erase_from_watcher_ = cloud_erase_from_watcher;
  return *this;
}
FakeCloudProvider::Builder& FakeCloudProvider::Builder::SetOnWatcherSet(
    fit::closure on_watcher_set) {
  on_watcher_set_ = std::move(on_watcher_set);
  return *this;
}

std::unique_ptr<FakeCloudProvider> FakeCloudProvider::Builder::Build() && {
  return std::make_unique<FakeCloudProvider>(std::move(*this));
}

FakeCloudProvider::FakeCloudProvider(Builder&& builder)
    : dispatcher_(builder.dispatcher_),
      random_(builder.random_),
      device_set_(builder.cloud_erase_on_check_, builder.cloud_erase_from_watcher_,
                  std::move(builder.on_watcher_set_)),
      page_clouds_(builder.dispatcher_),
      inject_network_error_(builder.inject_network_error_),
      inject_missing_diff_(builder.inject_missing_diff_) {}

FakeCloudProvider::FakeCloudProvider(async_dispatcher_t* dispatcher, Random* random)
    : FakeCloudProvider(Builder(dispatcher, random)) {}

FakeCloudProvider::~FakeCloudProvider() = default;

void FakeCloudProvider::GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
                                     GetDeviceSetCallback callback) {
  device_set_.AddBinding(std::move(device_set));
  callback(cloud_provider::Status::OK);
}

void FakeCloudProvider::GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                                     fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
                                     GetPageCloudCallback callback) {
  const std::string key = convert::ToString(app_id) + "_" + convert::ToString(page_id);
  auto ret = page_clouds_.try_emplace(std::move(key), dispatcher_, random_, inject_network_error_,
                                      inject_missing_diff_);
  ret.first->second.Bind(std::move(page_cloud));
  callback(cloud_provider::Status::OK);
}

}  // namespace ledger
