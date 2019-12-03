// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_CLOUD_PROVIDER_H_
#define SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_CLOUD_PROVIDER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <map>
#include <memory>
#include <vector>

#include "src/ledger/bin/cloud_sync/impl/testing/test_device_set.h"
#include "src/ledger/bin/cloud_sync/impl/testing/test_page_cloud.h"

namespace cloud_sync {
class TestCloudProvider : public cloud_provider::CloudProvider {
 public:
  explicit TestCloudProvider(fidl::InterfaceRequest<cloud_provider::CloudProvider> request);
  TestCloudProvider(const TestCloudProvider&) = delete;
  TestCloudProvider& operator=(const TestCloudProvider&) = delete;
  ~TestCloudProvider() override;

  TestDeviceSet device_set;
  std::vector<std::string> page_ids_requested;

 private:
  // cloud_provider::CloudProvider:
  void GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> request,
                    GetDeviceSetCallback callback) override;

  void GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                    fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
                    GetPageCloudCallback callback) override;

  fidl::Binding<cloud_provider::CloudProvider> binding_;
  fidl::Binding<cloud_provider::DeviceSet> device_set_binding_;
  std::map<std::pair<std::string, std::string>, std::unique_ptr<TestPageCloud>> page_cloud_;
};

}  // namespace cloud_sync

#endif  // SRC_LEDGER_BIN_CLOUD_SYNC_IMPL_TESTING_TEST_CLOUD_PROVIDER_H_
