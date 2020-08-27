// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/verified/llcpp/fidl.h>

#include <zxtest/zxtest.h>

#include "config.h"

namespace {

TEST(ConfigCheckTest, Accepts4kBlockSHA256HashFunction) {
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::HashFunction> hash_function =
      ::llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256;
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::BlockSize> block_size =
      ::llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096;
  auto config = ::llcpp::fuchsia::hardware::block::verified::Config::Builder(
                    std::make_unique<::llcpp::fuchsia::hardware::block::verified::Config::Frame>())
                    .set_hash_function(fidl::unowned_ptr(&hash_function))
                    .set_block_size(fidl::unowned_ptr(&block_size))
                    .build();

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_OK(block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingHashFunction) {
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::BlockSize> block_size =
      ::llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096;
  auto config = ::llcpp::fuchsia::hardware::block::verified::Config::Builder(
                    std::make_unique<::llcpp::fuchsia::hardware::block::verified::Config::Frame>())
                    .set_block_size(fidl::unowned_ptr(&block_size))
                    .build();

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsMissingBlockSize) {
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::HashFunction> hash_function =
      ::llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256;
  auto config = ::llcpp::fuchsia::hardware::block::verified::Config::Builder(
                    std::make_unique<::llcpp::fuchsia::hardware::block::verified::Config::Frame>())
                    .set_hash_function(fidl::unowned_ptr(&hash_function))
                    .build();

  block_info_t blk;
  blk.block_size = 4096;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

TEST(ConfigCheckTest, RejectsIfBlockSizeMismatch) {
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::HashFunction> hash_function =
      ::llcpp::fuchsia::hardware::block::verified::HashFunction::SHA256;
  fidl::aligned<::llcpp::fuchsia::hardware::block::verified::BlockSize> block_size =
      ::llcpp::fuchsia::hardware::block::verified::BlockSize::SIZE_4096;
  auto config = ::llcpp::fuchsia::hardware::block::verified::Config::Builder(
                    std::make_unique<::llcpp::fuchsia::hardware::block::verified::Config::Frame>())
                    .set_hash_function(fidl::unowned_ptr(&hash_function))
                    .set_block_size(fidl::unowned_ptr(&block_size))
                    .build();

  block_info_t blk;
  blk.block_size = 512;

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, block_verity::CheckConfig(config, blk));
}

}  // namespace
