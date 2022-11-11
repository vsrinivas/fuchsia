// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/fvm.h"

#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/storage/fs_management/cpp/fvm_internal.h"

namespace fs_management {
namespace {

constexpr uuid::Uuid kValidTypeGUID = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr uuid::Uuid kValidInstanceGUID = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                           0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
constexpr uuid::Uuid kInvalidGUID1 = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                      0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f};
constexpr uuid::Uuid kInvalidGUID2 = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                      0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};

constexpr std::string_view kValidLabel = "test";
constexpr std::string_view kInvalidLabel1 = "TheWrongLabel";
constexpr std::string_view kInvalidLabel2 = "StillTheWrongLabel";
constexpr std::string_view kDefaultPath = "/fake/block/device/1/partition/001";
constexpr std::string_view kParent = "/fake/block/device/1";
constexpr std::string_view kNotParent = "/fake/block/device/2";

class FakePartition
    : public fidl::testing::WireTestBase<fuchsia_hardware_block_partition::PartitionAndDevice> {
 public:
  FakePartition(const uuid::Uuid& type, const uuid::Uuid& instance, std::string_view label,
                std::string_view path)
      : type_guid_(type), instance_guid_(instance), label_(label), path_(path) {}

  void NotImplemented_(const std::string& name, ::fidl::CompleterBase& completer) override {
    printf("'%s' was called unexpectedly", name.c_str());
    ASSERT_TRUE(false);
  }

  void GetTypeGuid(GetTypeGuidCompleter::Sync& completer) override {
    fuchsia_hardware_block_partition::wire::Guid guid;
    std::copy(type_guid_.begin(), type_guid_.end(), guid.value.begin());
    auto guid_object =
        fidl::ObjectView<fuchsia_hardware_block_partition::wire::Guid>(allocator_, guid);
    completer.Reply(ZX_OK, guid_object);
  }

  void GetInstanceGuid(GetInstanceGuidCompleter::Sync& completer) override {
    fuchsia_hardware_block_partition::wire::Guid guid;
    std::copy(instance_guid_.begin(), instance_guid_.end(), guid.value.begin());
    auto guid_object =
        fidl::ObjectView<fuchsia_hardware_block_partition::wire::Guid>(allocator_, guid);
    completer.Reply(ZX_OK, guid_object);
  }

  void GetName(GetNameCompleter::Sync& completer) override {
    auto label_object = fidl::StringView(allocator_, label_);
    completer.Reply(ZX_OK, label_object);
  }

  void GetTopologicalPath(GetTopologicalPathCompleter::Sync& completer) override {
    completer.ReplySuccess(fidl::StringView::FromExternal(path_));
  }

  zx::result<fidl::ClientEnd<fuchsia_hardware_block_partition::PartitionAndDevice>> GetClient(
      async_dispatcher_t* dispatcher) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_block_partition::PartitionAndDevice>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    fidl::BindServer(dispatcher, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }

 private:
  fidl::Arena<1024> allocator_;
  uuid::Uuid type_guid_;
  uuid::Uuid instance_guid_;
  std::string_view label_;
  std::string_view path_;
};

class PartitionMatchesTest : public testing::Test {
 public:
  PartitionMatchesTest()
      : partition_(kValidTypeGUID, kValidInstanceGUID, kValidLabel, kDefaultPath),
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
  fidl::ClientEnd<fuchsia_hardware_block_partition::PartitionAndDevice> client_;
};

TEST_F(PartitionMatchesTest, TestTypeMatch) {
  const PartitionMatcher kMatcher = {.type_guids = {kInvalidGUID1, kValidTypeGUID, kInvalidGUID2}};
  ASSERT_TRUE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestInstanceMatch) {
  const PartitionMatcher kMatcher = {
      .instance_guids = {kInvalidGUID1, kValidInstanceGUID, kInvalidGUID2}};
  ASSERT_TRUE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestTypeAndInstanceMatch) {
  const PartitionMatcher kMatcher = {
      .type_guids = {kInvalidGUID1, kValidTypeGUID, kInvalidGUID2},
      .instance_guids = {kInvalidGUID1, kValidInstanceGUID, kInvalidGUID2}};
  ASSERT_TRUE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestParentMatch) {
  {
    const PartitionMatcher kMatcher = {.parent_device = kParent};
    ASSERT_TRUE(PartitionMatches(client_, kMatcher));
  }

  {
    const PartitionMatcher kMatcher = {.parent_device = kNotParent};
    ASSERT_FALSE(PartitionMatches(client_, kMatcher));
  }
}

TEST_F(PartitionMatchesTest, TestLabelMatch) {
  const PartitionMatcher kMatcher = {.labels = {
                                         kInvalidLabel1,
                                         kValidLabel,
                                         kInvalidLabel2,
                                     }};
  ASSERT_TRUE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestTypeAndLabelMatch) {
  const PartitionMatcher kMatcher = {
      .type_guids = {kValidTypeGUID},
      .labels = {kValidLabel},
  };
  ASSERT_TRUE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestTypeMismatch) {
  const PartitionMatcher kMatcher = {.type_guids = {kInvalidGUID1}};
  ASSERT_FALSE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestInstanceMismatch) {
  const PartitionMatcher kMatcher = {.type_guids = {kValidTypeGUID},
                                     .instance_guids = {kInvalidGUID2}};
  ASSERT_FALSE(PartitionMatches(client_, kMatcher));
}

TEST_F(PartitionMatchesTest, TestLabelMismatch) {
  const PartitionMatcher kMatcher = {.type_guids = {kValidTypeGUID},
                                     .labels = {
                                         kInvalidLabel1,
                                         kInvalidLabel2,
                                     }};
  ASSERT_FALSE(PartitionMatches(client_, kMatcher));
}

}  // namespace
}  // namespace fs_management
