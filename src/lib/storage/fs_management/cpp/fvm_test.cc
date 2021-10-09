// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm.h"

#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "lib/zx/channel.h"
#include "src/lib/fxl/test/test_settings.h"

constexpr uint8_t kValidTypeGUID[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr uint8_t kValidInstanceGUID[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                          0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
constexpr uint8_t kInvalidGUID1[] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                     0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f};
constexpr uint8_t kInvalidGUID2[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                     0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};

constexpr char kValidLabel[] = "test";
constexpr char kInvalidLabel1[] = "TheWrongLabel";
constexpr char kInvalidLabel2[] = "StillTheWrongLabel";

class FakePartition : public fuchsia_hardware_block_partition::testing::Partition_TestBase {
 public:
  FakePartition(const uint8_t* type_guid, const uint8_t* instance_guid, const char* label)
      : type_guid_(type_guid), instance_guid_(instance_guid), label_(label){};

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    printf("'%s' was called unexpectedly", name.c_str());
    ASSERT_TRUE(false);
  }

  void GetTypeGuid(GetTypeGuidRequestView _request,
                   GetTypeGuidCompleter::Sync& completer) override {
    fuchsia_hardware_block_partition::wire::Guid guid;
    memcpy(guid.value.data_, type_guid_, 16);
    auto guid_object =
        fidl::ObjectView<fuchsia_hardware_block_partition::wire::Guid>(allocator_, guid);
    completer.Reply(ZX_OK, std::move(guid_object));
  }

  void GetInstanceGuid(GetInstanceGuidRequestView _request,
                       GetInstanceGuidCompleter::Sync& completer) override {
    fuchsia_hardware_block_partition::wire::Guid guid;
    memcpy(guid.value.data(), instance_guid_, 16);
    auto guid_object =
        fidl::ObjectView<fuchsia_hardware_block_partition::wire::Guid>(allocator_, guid);
    completer.Reply(ZX_OK, std::move(guid_object));
  }

  void GetName(GetNameRequestView _request, GetNameCompleter::Sync& completer) override {
    auto label_object = fidl::StringView(allocator_, label_);
    completer.Reply(ZX_OK, std::move(label_object));
  }

  zx::status<fidl::ClientEnd<fuchsia_hardware_block_partition::Partition>> GetClient(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_block_partition::Partition>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    fidl::BindServer(dispatcher, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }

 private:
  fidl::Arena<1024> allocator_;
  const uint8_t* type_guid_;
  const uint8_t* instance_guid_;
  const char* label_;
};

class PartitionMatchesTest : public testing::Test {
 public:
  PartitionMatchesTest()
      : partition_(kValidTypeGUID, kValidInstanceGUID, kValidLabel),
        loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    ASSERT_EQ(loop_.StartThread("test-fidl-loop"), ZX_OK);
    auto client = partition_.GetClient(loop_.dispatcher());
    ASSERT_EQ(client.status_value(), ZX_OK);
    client_ = std::move(client.value());
  }

 protected:
  FakePartition partition_;
  async::Loop loop_;
  fidl::ClientEnd<fuchsia_hardware_block_partition::Partition> client_;
};

TEST_F(PartitionMatchesTest, TestTypeMatch) {
  PartitionMatcher matcher = {.type_guid = kValidTypeGUID};
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestInstanceMatch) {
  PartitionMatcher matcher = {.instance_guid = kValidInstanceGUID};
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestTypeAndInstanceMatch) {
  PartitionMatcher matcher = {.type_guid = kValidTypeGUID, .instance_guid = kValidInstanceGUID};
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestSingleLabelMatch) {
  constexpr std::array<const char*, 1> kLabels{
      kValidLabel,
  };
  PartitionMatcher matcher = {
      .labels = kLabels.data(),
      .num_labels = kLabels.size(),
  };
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestMultiLabelMatch) {
  constexpr std::array<const char*, 3> kLabels{
      kInvalidLabel1,
      kValidLabel,
      kInvalidLabel2,
  };
  PartitionMatcher matcher = {
      .labels = kLabels.data(),
      .num_labels = kLabels.size(),
  };
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestTypeAndLabelMatch) {
  constexpr std::array<const char*, 1> kLabels{
      kValidLabel,
  };
  PartitionMatcher matcher = {
      .type_guid = kValidTypeGUID,
      .labels = kLabels.data(),
      .num_labels = kLabels.size(),
  };
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_TRUE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestTypeMismatch) {
  PartitionMatcher matcher = {.type_guid = kInvalidGUID1};
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_FALSE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestInstanceMismatch) {
  PartitionMatcher matcher = {.type_guid = kValidTypeGUID, .instance_guid = kInvalidGUID2};
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_FALSE(fs_management::PartitionMatches(channel, matcher));
}

TEST_F(PartitionMatchesTest, TestLabelMismatch) {
  constexpr std::array<const char*, 2> kLabels{
      kInvalidLabel1,
      kInvalidLabel2,
  };
  PartitionMatcher matcher = {
      .type_guid = kValidTypeGUID,
      .labels = kLabels.data(),
      .num_labels = kLabels.size(),
  };
  zx::unowned_channel channel = client_.borrow().channel();
  ASSERT_FALSE(fs_management::PartitionMatches(channel, matcher));
}
