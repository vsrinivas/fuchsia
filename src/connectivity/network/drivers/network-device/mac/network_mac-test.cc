// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "test_util.h"

namespace network {
namespace testing {

class MacDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    // enable full tracing for tests, easier to debug problems.
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
  }

  void TearDown() override {
    if (device_) {
      sync_completion_t completion;
      device_->Teardown([&completion]() { sync_completion_signal(&completion); });
      sync_completion_wait(&completion, ZX_TIME_INFINITE);
    }
  }

  zx_status_t CreateDevice() {
    if (device_) {
      return ZX_ERR_INTERNAL;
    }
    zx::result device = impl_.CreateChild();
    if (device.is_ok()) {
      device_ = std::move(device.value());
    }
    return device.status_value();
  }

  zx::result<fidl::WireSyncClient<netdev::MacAddressing>> OpenInstance() {
    zx::result endpoints = fidl::CreateEndpoints<netdev::MacAddressing>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto [client_end, server_end] = std::move(*endpoints);
    // Create the loop with a test thread lazily.
    if (!loop_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      zx_status_t status = loop_->StartThread("test-thread", nullptr);
      if (status != ZX_OK) {
        return zx::error(status);
      }
    }
    if (zx_status_t status = device_->Bind(loop_->dispatcher(), std::move(server_end));
        status != ZX_OK) {
      return zx::error(status);
    }

    // Every time a new client is bound, we're going to get signaled for a
    // configuration change.
    if (zx_status_t status = impl_.WaitConfigurationChanged(); status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(fidl::WireSyncClient(std::move(client_end)));
  }

 protected:
  FakeMacDeviceImpl impl_;
  // Loop whose dispatcher is used to create client instances.
  // The loop is created lazily in `OpenInstance` to avoid spawning threads on tests that do not
  // instantiate clients.
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<MacAddrDeviceInterface> device_;
};

MATCHER_P(MacEq, value, "") {
  return std::equal(arg.octets.begin(), arg.octets.end(), value.octets.begin(), value.octets.end());
}

TEST_F(MacDeviceTest, GetAddress) {
  ASSERT_OK(CreateDevice());
  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();
  fidl::WireResult result = client->GetUnicastAddress();
  ASSERT_OK(result.status());
  ASSERT_THAT(result.value().address, MacEq(impl_.mac()));
}

TEST_F(MacDeviceTest, UnrecognizedMode) {
  // set some arbitrary not supported mode:
  impl_.features().supported_modes |= (1u << 31u);
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MacDeviceTest, EmptyMode) {
  // set an empty set for supported modes
  impl_.features().supported_modes = 0;
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MacDeviceTest, StartupModeFilter) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
}

TEST_F(MacDeviceTest, StartupModeMcastPromiscuous) {
  impl_.features().supported_modes = MODE_MULTICAST_PROMISCUOUS | MODE_PROMISCUOUS;
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_PROMISCUOUS);
}

TEST_F(MacDeviceTest, StartupModePromiscuous) {
  impl_.features().supported_modes = MODE_PROMISCUOUS;
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);
}

TEST_F(MacDeviceTest, SetBadMode) {
  impl_.features().supported_modes = MODE_MULTICAST_FILTER;
  ASSERT_OK(CreateDevice());
  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  fidl::WireResult result = client->SetMode(netdev::wire::MacFilterMode::kPromiscuous);
  ASSERT_OK(result.status());
  ASSERT_STATUS(result.value().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MacDeviceTest, SetPromiscuous) {
  ASSERT_OK(CreateDevice());
  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  fidl::WireResult result = client->SetMode(netdev::wire::MacFilterMode::kPromiscuous);
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, SetMulticastPromiscuous) {
  ASSERT_OK(CreateDevice());
  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  fidl::WireResult result = client->SetMode(netdev::wire::MacFilterMode::kMulticastPromiscuous);
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, InvalidMulticastAddress) {
  ASSERT_OK(CreateDevice());
  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  MacAddress addr{{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}};
  fidl::WireResult add = client->AddMulticastAddress(addr);
  ASSERT_OK(add.status());
  ASSERT_STATUS(add.value().status, ZX_ERR_INVALID_ARGS);

  // same thing should happen for RemoveMulticastAddress:
  fidl::WireResult remove = client->RemoveMulticastAddress(addr);
  ASSERT_OK(remove.status());
  ASSERT_STATUS(remove.value().status, ZX_ERR_INVALID_ARGS);
}

MATCHER(MacEq, "") {
  auto [left, right] = arg;

  return std::equal(left.octets.begin(), left.octets.end(), right.octets.begin(),
                    right.octets.end());
}

TEST_F(MacDeviceTest, AddRemoveMulticastFilter) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  MacAddress addr{{0x01, 0x01, 0x02, 0x03, 0x04, 0x05}};
  fidl::WireResult add = client->AddMulticastAddress(addr);
  ASSERT_OK(add.status());
  ASSERT_OK(add.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_THAT(impl_.addresses(), ::testing::Pointwise(MacEq(), {addr}));

  fidl::WireResult remove = client->RemoveMulticastAddress(addr);
  ASSERT_OK(remove.status());
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, OverflowsIntoMulticastPromiscuous) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  for (size_t i = 0; i < impl_.features().multicast_filter_count + 1; i++) {
    MacAddress addr{{0x01, 0x00, 0x00, 0x00, 0x00, static_cast<unsigned char>(i)}};
    fidl::WireResult result = client->AddMulticastAddress(addr);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
  }
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, MostPermissiveClientWins) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  zx::result open_result1 = OpenInstance();
  ASSERT_OK(open_result1.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& cli1 = open_result1.value();

  zx::result open_result2 = OpenInstance();
  ASSERT_OK(open_result2.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& cli2 = open_result2.value();

  MacAddress addr{{0x01, 0x00, 0x00, 0x00, 0x00, 0x02}};
  {
    fidl::WireResult result = cli1->AddMulticastAddress(addr);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
    ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
    ASSERT_EQ(impl_.addresses().size(), 1ul);
  }
  {
    fidl::WireResult result = cli2->SetMode(netdev::wire::MacFilterMode::kPromiscuous);
    ASSERT_OK(result.status());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
    ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);
    ASSERT_TRUE(impl_.addresses().empty());
  }
  // Remove second instance and check that the mode fell back to the first one.
  cli2 = {};
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_EQ(impl_.addresses().size(), 1ul);
}

TEST_F(MacDeviceTest, FallsBackToDefaultMode) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  zx::result open_result = OpenInstance();
  ASSERT_OK(open_result.status_value());
  fidl::WireSyncClient<netdev::MacAddressing>& client = open_result.value();

  fidl::WireResult result = client->SetMode(netdev::wire::MacFilterMode::kPromiscuous);
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);

  // close the instance and check that we fell back to the default mode.
  client = {};
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_TRUE(impl_.addresses().empty());
}

}  // namespace testing
}  // namespace network
