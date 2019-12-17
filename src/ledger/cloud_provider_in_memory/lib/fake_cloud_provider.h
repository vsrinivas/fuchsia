// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "src/ledger/bin/fidl_helpers/bound_interface_set.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_device_set.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_page_cloud.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/ledger/lib/callback/auto_cleanable.h"

namespace ledger {

class FakeCloudProvider : public cloud_provider::CloudProvider {
 public:
  class Builder {
   public:
    Builder(async_dispatcher_t* dispatcher, Random* random);
    ~Builder();

    Builder& SetInjectNetworkError(InjectNetworkError inject_network_error);
    Builder& SetInjectMissingDiff(InjectMissingDiff inject_missing_diff);
    Builder& SetCloudEraseOnCheck(CloudEraseOnCheck cloud_erase_on_check);
    Builder& SetCloudEraseFromWatcher(CloudEraseFromWatcher cloud_erase_from_watcher);
    // |on_watcher_set| will be called every time a watcher is set.
    Builder& SetOnWatcherSet(fit::closure on_watcher_set);

    std::unique_ptr<FakeCloudProvider> Build() &&;

   private:
    friend FakeCloudProvider;

    async_dispatcher_t* const dispatcher_;
    Random* const random_;
    InjectNetworkError inject_network_error_ = InjectNetworkError::NO;
    InjectMissingDiff inject_missing_diff_ = InjectMissingDiff::NO;
    CloudEraseOnCheck cloud_erase_on_check_ = CloudEraseOnCheck::NO;
    CloudEraseFromWatcher cloud_erase_from_watcher_ = CloudEraseFromWatcher::NO;
    fit::closure on_watcher_set_ = nullptr;
  };

  explicit FakeCloudProvider(async_dispatcher_t* dispatcher, Random* random);
  explicit FakeCloudProvider(Builder&& builder);
  FakeCloudProvider(const FakeCloudProvider&) = delete;
  FakeCloudProvider& operator=(const FakeCloudProvider&) = delete;
  ~FakeCloudProvider() override;

 private:
  void GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
                    GetDeviceSetCallback callback) override;

  void GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                    fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
                    GetPageCloudCallback callback) override;

  async_dispatcher_t* const dispatcher_;
  Random* const random_;

  fidl_helpers::BoundInterfaceSet<cloud_provider::DeviceSet, FakeDeviceSet> device_set_;

  AutoCleanableMap<std::string, FakePageCloud> page_clouds_;

  InjectNetworkError inject_network_error_;
  InjectMissingDiff inject_missing_diff_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_
