// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/fidl/cpp/binding.h>

#include "src/ledger/bin/fidl_helpers/bound_interface_set.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_device_set.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_page_cloud.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/lib/fxl/macros.h"

namespace ledger {

class FakeCloudProvider : public cloud_provider::CloudProvider {
 public:
  class Builder {
   public:
    Builder();
    ~Builder();

    Builder& SetInjectNetworkError(InjectNetworkError inject_network_error);
    Builder& SetCloudEraseOnCheck(CloudEraseOnCheck cloud_erase_on_check);
    Builder& SetCloudEraseFromWatcher(
        CloudEraseFromWatcher cloud_erase_from_watcher);

    std::unique_ptr<FakeCloudProvider> Build();

   private:
    friend FakeCloudProvider;

    InjectNetworkError inject_network_error_ = InjectNetworkError::NO;
    CloudEraseOnCheck cloud_erase_on_check_ = CloudEraseOnCheck::NO;
    CloudEraseFromWatcher cloud_erase_from_watcher_ = CloudEraseFromWatcher::NO;
  };

  FakeCloudProvider();
  explicit FakeCloudProvider(const Builder& builder);
  ~FakeCloudProvider() override;

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      GetDeviceSetCallback callback) override;

  void GetPageCloud(
      std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      GetPageCloudCallback callback) override;

  fidl_helpers::BoundInterfaceSet<cloud_provider::DeviceSet, FakeDeviceSet>
      device_set_;

  callback::AutoCleanableMap<std::string, FakePageCloud> page_clouds_;

  InjectNetworkError inject_network_error_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCloudProvider);
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_CLOUD_PROVIDER_H_
