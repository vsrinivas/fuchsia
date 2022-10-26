// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

constexpr uint64_t kDefaultMaxFileSize = uint64_t{512} * 1024 * 1024;

struct TestParameter {
  std::optional<uint64_t> max_file_size;
  const char* test_name;
};

class MemfsComponentTest : public ::gtest::RealLoopFixture,
                           public ::testing::WithParamInterface<TestParameter> {
 protected:
  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ConfigValue;
    using component_testing::Directory;
    using component_testing::ParentRef;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr const char* kMemfsChild = "memfs";
    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kMemfsChild, "#meta/memfs.cm");
    realm_builder.AddRoute(Route{.capabilities =
                                     {
                                         Directory{.name = "memfs", .path = "/root"},
                                     },
                                 .source = ChildRef{kMemfsChild},
                                 .targets = {ParentRef()}});

    // Override max_file_size structured config.
    realm_builder.InitMutableConfigFromPackage("memfs");
    if (GetParam().max_file_size) {
      realm_builder.SetConfigValue(kMemfsChild, "max_file_size",
                                   ConfigValue::Uint64(*GetParam().max_file_size));
    }

    realm_root_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    auto root = realm_root_->CloneRoot();
    zx_status_t status =
        fdio_fd_create(root.TakeChannel().release(), root_fd_.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);
  }

  fbl::unique_fd root_fd_;
  std::unique_ptr<component_testing::RealmRoot> realm_root_;
};

TEST_P(MemfsComponentTest, MaxFileSize) {
  int fd = openat(root_fd_.get(), "memfs/test_file", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  ASSERT_GE(fd, 0);

  // First test that we can create the largest valid file size.
  off_t max_valid_file_size = GetParam().max_file_size.value_or(kDefaultMaxFileSize);
  int result = ftruncate(fd, max_valid_file_size);
  ASSERT_EQ(0, result);

  // Now test creating the smallest invalid file size. This should fail.
  result = ftruncate(fd, max_valid_file_size + 1);
  ASSERT_EQ(-1, result);
}

INSTANTIATE_TEST_SUITE_P(MemfsComponentTest, MemfsComponentTest,
                         testing::Values(TestParameter{std::nullopt, "default"},
                                         TestParameter{uint64_t{512} * 1024 * 1024, "512MiB"},
                                         TestParameter{uint64_t{4} * 1024 * 1024 * 1024, "4GiB"}),
                         [](const testing::TestParamInfo<TestParameter>& info) {
                           return info.param.test_name;
                         });

}  // namespace
