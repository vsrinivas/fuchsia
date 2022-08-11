// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_config/guest_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class GuestConfigParserTest : public ::testing::Test {
 protected:
  std::vector<std::string> paths_;
  fuchsia::virtualization::GuestConfig config_;

  zx_status_t ParseConfig(const std::string& config_str) {
    auto open_at = [this](const std::string& path, auto) {
      paths_.emplace_back(path);
      return ZX_OK;
    };
    auto st = guest_config::ParseConfig(config_str, std::move(open_at), &config_);
    if (st == ZX_OK) {
      guest_config::SetDefaults(&config_);
    }
    return st;
  }
};

TEST_F(GuestConfigParserTest, DefaultValues) {
  ASSERT_EQ(ZX_OK, ParseConfig("{}"));
  ASSERT_FALSE(config_.has_kernel_type());
  ASSERT_FALSE(config_.has_kernel());
  ASSERT_FALSE(config_.has_ramdisk());
  ASSERT_EQ(zx_system_get_num_cpus(), config_.cpus());
  ASSERT_FALSE(config_.has_block_devices());
  ASSERT_FALSE(config_.has_cmdline());
  ASSERT_EQ(zx_system_get_physmem() - std::min(zx_system_get_physmem() / 2, 3 * (1ul << 30)),
            config_.guest_memory());
}

TEST_F(GuestConfigParserTest, ParseConfig) {
  ASSERT_EQ(ZX_OK, ParseConfig(
                       R"JSON({
          "zircon": "zircon_path",
          "ramdisk": "ramdisk_path",
          "cpus": "4",
          "block": "/pkg/data/block_path",
          "cmdline": "kernel cmdline"
        })JSON"));
  ASSERT_EQ(fuchsia::virtualization::KernelType::ZIRCON, config_.kernel_type());
  ASSERT_TRUE(config_.kernel());
  ASSERT_TRUE(config_.ramdisk());
  ASSERT_EQ(4u, config_.cpus());
  ASSERT_EQ(1ul, config_.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config_.block_devices().front().id);
  ASSERT_EQ("kernel cmdline", config_.cmdline());
}

TEST_F(GuestConfigParserTest, BlockSpecJson) {
  ASSERT_EQ(ZX_OK, ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,file",
            "/dev/class/block/001,rw,file"
          ]
        })JSON"));
  ASSERT_EQ(2ul, config_.block_devices().size());

  const fuchsia::virtualization::BlockSpec& spec0 = config_.block_devices()[0];
  ASSERT_EQ("/pkg/data/foo", spec0.id);
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::FILE, spec0.format);

  const fuchsia::virtualization::BlockSpec& spec1 = config_.block_devices()[1];
  ASSERT_EQ("/dev/class/block/001", spec1.id);
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::FILE, spec1.format);

  EXPECT_THAT(paths_, testing::ElementsAre("/pkg/data/foo", "/dev/class/block/001"));
}
