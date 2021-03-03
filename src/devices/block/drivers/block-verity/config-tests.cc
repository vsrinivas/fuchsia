// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/verified/llcpp/fidl.h>

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/block-verity/config.h"

namespace {

TEST(ConfigCheckTest, Accepts4kBlockSHA256HashFunction) {
  fidl::aligned<fuchsia_hardware_block_verified::wire::HashFunction> hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::SHA256;
  fidl::aligned<fuchsia_hardware_block_verified::wire::BlockSize> block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::SIZE_4096;
  fidl::FidlAllocator allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fidl::unowned_ptr(&hash_function));
  config.set_block_size(fidl::unowned_ptr(&block_size));

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_OK(block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, Accepts4kBlockSHA256HashFunction512BackingBlockSize) {
  fidl::aligned<fuchsia_hardware_block_verified::wire::HashFunction> hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::SHA256;
  fidl::aligned<fuchsia_hardware_block_verified::wire::BlockSize> block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::SIZE_4096;
  fidl::FidlAllocator allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fidl::unowned_ptr(&hash_function));
  config.set_block_size(fidl::unowned_ptr(&block_size));

  block_info_t blk;
  blk.block_size = 512;

  EXPECT_OK(block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingHashFunction) {
  fidl::aligned<fuchsia_hardware_block_verified::wire::BlockSize> block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::SIZE_4096;
  fidl::FidlAllocator allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_block_size(fidl::unowned_ptr(&block_size));

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingBlockSize) {
  fidl::aligned<fuchsia_hardware_block_verified::wire::HashFunction> hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::SHA256;
  fidl::FidlAllocator allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fidl::unowned_ptr(&hash_function));

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsIfBlockSizeUnsupportable) {
  fidl::aligned<fuchsia_hardware_block_verified::wire::HashFunction> hash_function =
      fuchsia_hardware_block_verified::wire::HashFunction::SHA256;
  fidl::aligned<fuchsia_hardware_block_verified::wire::BlockSize> block_size =
      fuchsia_hardware_block_verified::wire::BlockSize::SIZE_4096;
  fidl::FidlAllocator allocator;
  fuchsia_hardware_block_verified::wire::Config config(allocator);
  config.set_hash_function(fidl::unowned_ptr(&hash_function));
  config.set_block_size(fidl::unowned_ptr(&block_size));

  block_info_t blk;
  // not a divisor of 4k
  blk.block_size = 640;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
  blk.block_size = 8192;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

}  // namespace
