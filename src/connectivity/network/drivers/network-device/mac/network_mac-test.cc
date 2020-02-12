// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <zxtest/cpp/zxtest.h>
#include <zxtest/zxtest.h>

#include "test_util.h"

namespace network {
namespace testing {

class MacDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    // enable full tracing for tests, easier to debug problems.
    fx_logger_config_t log_cfg = {
        .min_severity = -2,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_init_with_config(&log_cfg);
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
    return impl_.CreateChild(&device_);
  }

  fit::result<netdev::MacAddressing::SyncClient, zx_status_t> OpenInstance() {
    zx::channel channel, req;
    zx_status_t status;
    if ((status = zx::channel::create(0, &channel, &req)) != ZX_OK) {
      return fit::error(status);
    }
    // Create the loop with a test thread lazily.
    if (!loop_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      if ((status = loop_->StartThread("test-thread", nullptr)) != ZX_OK) {
        return fit::error(status);
      }
    }
    status = device_->Bind(loop_->dispatcher(), std::move(req));
    if (status != ZX_OK) {
      return fit::error(status);
    }
    return fit::ok(netdev::MacAddressing::SyncClient(std::move(channel)));
  }

 protected:
  FakeMacDeviceImpl impl_;
  // Loop whose dispatcher is used to create client instances.
  // The loop is created lazily in `OpenInstance` to avoid spawning threads on tests that do not
  // instantiate clients.
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<MacAddrDeviceInterface> device_;
};

TEST_F(MacDeviceTest, GetAddress) {
  ASSERT_OK(CreateDevice());
  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();
  auto result = client.GetUnicastAddress();
  ASSERT_TRUE(result.ok());
  ASSERT_BYTES_EQ(result.value().address.octets.data(), impl_.mac(), MAC_SIZE);
}

TEST_F(MacDeviceTest, UnrecognizedMode) {
  // set some arbitray not supported mode:
  impl_.features().supported_modes |= (1u << 31u);
  ASSERT_EQ(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MacDeviceTest, EmptyMode) {
  // set an empty set for supported modes
  impl_.features().supported_modes = 0;
  ASSERT_EQ(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
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
  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  auto result = client.SetMode(netdev::MacFilterMode::PROMISCUOUS);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(MacDeviceTest, SetPromiscuous) {
  ASSERT_OK(CreateDevice());
  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  auto result = client.SetMode(netdev::MacFilterMode::PROMISCUOUS);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().status, ZX_OK);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, SetMulticastPromiscuous) {
  ASSERT_OK(CreateDevice());
  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  auto result = client.SetMode(netdev::MacFilterMode::MULTICAST_PROMISCUOUS);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, InvalidMulticastAddress) {
  ASSERT_OK(CreateDevice());
  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  MacAddress addr{{0x00, 0x01, 0x02, 0x03, 0x04, 0x05}};
  auto add = client.AddMulticastAddress(addr);
  ASSERT_TRUE(add.ok());
  ASSERT_EQ(add.value().status, ZX_ERR_INVALID_ARGS);

  // same thing should happen for RemoveMulticastAddress:
  auto remove = client.RemoveMulticastAddress(addr);
  ASSERT_TRUE(remove.ok());
  ASSERT_EQ(remove.value().status, ZX_ERR_INVALID_ARGS);
}

TEST_F(MacDeviceTest, AddRemoveMulticastFilter) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  MacAddress addr{{0x01, 0x01, 0x02, 0x03, 0x04, 0x05}};
  auto add = client.AddMulticastAddress(addr);
  ASSERT_TRUE(add.ok());
  ASSERT_OK(add.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_EQ(impl_.addresses().size(), 1ul);
  ASSERT_BYTES_EQ(impl_.addresses()[0].octets.data(), addr.octets.data(), MAC_SIZE);

  auto remove = client.RemoveMulticastAddress(addr);
  ASSERT_TRUE(remove.ok());
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, OverflowsIntoMulticastPromiscuous) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  for (size_t i = 0; i < impl_.features().multicast_filter_count + 1; i++) {
    MacAddress addr{{0x01, 0x00, 0x00, 0x00, 0x00, static_cast<unsigned char>(i)}};
    auto result = client.AddMulticastAddress(addr);
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
  }
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_PROMISCUOUS);
  ASSERT_TRUE(impl_.addresses().empty());
}

TEST_F(MacDeviceTest, MostPermissiveClientWins) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto cli1 = open_result.take_value();

  open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto cli2 = open_result.take_value();

  MacAddress addr{{0x01, 0x00, 0x00, 0x00, 0x00, 0x02}};
  {
    auto result = cli1.AddMulticastAddress(addr);
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
    ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
    ASSERT_EQ(impl_.addresses().size(), 1ul);
  }
  {
    auto result = cli2.SetMode(netdev::MacFilterMode::PROMISCUOUS);
    ASSERT_TRUE(result.ok());
    ASSERT_OK(result.value().status);
    ASSERT_OK(impl_.WaitConfigurationChanged());
    ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);
    ASSERT_TRUE(impl_.addresses().empty());
  }
  // Remove second instance and check that the mode fell back to the first one.
  cli2.mutable_channel()->reset();
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_EQ(impl_.addresses().size(), 1ul);
}

TEST_F(MacDeviceTest, FallsBackToDefaultMode) {
  ASSERT_OK(CreateDevice());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);

  auto open_result = OpenInstance();
  ASSERT_TRUE(open_result.is_ok(), "OpenInstance failed: %s",
              zx_status_get_string(open_result.error()));
  auto client = open_result.take_value();

  auto result = client.SetMode(netdev::MacFilterMode::PROMISCUOUS);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.value().status);
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_PROMISCUOUS);

  // close the instance and check that we fell back to the default mode.
  client.mutable_channel()->reset();
  ASSERT_OK(impl_.WaitConfigurationChanged());
  ASSERT_EQ(impl_.mode(), MODE_MULTICAST_FILTER);
  ASSERT_TRUE(impl_.addresses().empty());
}

}  // namespace testing
}  // namespace network
