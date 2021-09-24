// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.block.verified/cpp/wire.h>

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/block-verity/config.h"

namespace {

TEST(ConfigCheckTest, Accepts4kBlockSHA256HashFunction) {
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256);
  config.set_block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096);

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_OK(block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, Accepts4kBlockSHA256HashFunction512BackingBlockSize) {
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256);
  config.set_block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096);

  block_info_t blk;
  blk.block_size = 512;

  EXPECT_OK(block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingHashFunction) {
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096);

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingBlockSize) {
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256);

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsIfBlockSizeUnsupportable) {
  fidl::Arena allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fuchsia_hardware_block_verified::wire::HashFunction::kSha256);
  config.set_block_size(fuchsia_hardware_block_verified::wire::BlockSize::kSize4096);

  block_info_t blk;
  // not a divisor of 4k
  blk.block_size = 640;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
  blk.block_size = 8192;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

}  // namespace
