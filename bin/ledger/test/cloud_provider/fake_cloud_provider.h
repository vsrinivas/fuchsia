// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_CLOUD_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_CLOUD_PROVIDER_H_

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/test/cloud_provider/fake_device_set.h"
#include "peridot/bin/ledger/test/cloud_provider/fake_page_cloud.h"

namespace ledger {

class FakeCloudProvider : public cloud_provider::CloudProvider {
 public:
  FakeCloudProvider();
  ~FakeCloudProvider() override;

 private:
  void GetDeviceSet(
      fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
      const GetDeviceSetCallback& callback) override;

  void GetPageCloud(
      fidl::Array<uint8_t> app_id,
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      const GetPageCloudCallback& callback) override;

  void EraseAllData(const EraseAllDataCallback& callback) override;

  callback::AutoCleanableSet<FakeDeviceSet> device_sets_;

  callback::AutoCleanableMap<std::string, FakePageCloud> page_clouds_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeCloudProvider);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_CLOUD_PROVIDER_H_
