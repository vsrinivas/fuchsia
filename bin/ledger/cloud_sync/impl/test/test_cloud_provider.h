// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_CLOUD_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_CLOUD_PROVIDER_H_

#include <memory>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/cloud_sync/impl/test/test_device_set.h"

namespace cloud_sync {
namespace test {

class TestCloudProvider : public cloud_provider::CloudProvider {
 public:
  explicit TestCloudProvider(
      fidl::InterfaceRequest<cloud_provider::CloudProvider> request);
  ~TestCloudProvider() override;

  TestDeviceSet device_set;

 private:
  // cloud_provider::CloudProvider:
  void GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> request,
                    const GetDeviceSetCallback& callback) override;

  void GetPageCloud(
      fidl::Array<uint8_t> app_id,
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
      const GetPageCloudCallback& callback) override;

  void EraseAllData(const EraseAllDataCallback& callback) override;

  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fidl::Binding<cloud_provider::DeviceSet> device_set_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestCloudProvider);
};

}  // namespace test
}  // namespace cloud_sync

#endif  // PERIDOT_BIN_LEDGER_CLOUD_SYNC_IMPL_TEST_TEST_CLOUD_PROVIDER_H_
