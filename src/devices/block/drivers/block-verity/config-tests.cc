// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.block.verified/cpp/wire.h>

#include <zxtest/zxtest.h>

#include "src/devices/block/drivers/block-verity/config.h"

namespace {

struct ConfigCheckTestParam {
  std::optional<fuchsia_hardware_block_verified::wire::HashFunction> hash_function;
  std::optional<fuchsia_hardware_block_verified::wire::BlockSize> block_size;
  block_info_t block;
  zx_status_t status;
};

class ConfigCheckTest : public zxtest::TestWithParam<ConfigCheckTestParam> {};

TEST_P(ConfigCheckTest, Checks) {
  fidl::Arena allocator;
  fidl::WireTableBuilder builder =
      fuchsia_hardware_block_verified::wire::Config::Builder(allocator);

  const ConfigCheckTestParam& param = GetParam();
  if (param.hash_function.has_value()) {
    builder.hash_function(param.hash_function.value());
  }
  if (param.block_size.has_value()) {
    builder.block_size(param.block_size.value());
  }

  EXPECT_STATUS(block_verity::CheckConfig(builder.Build(), param.block), param.status);
}

INSTANTIATE_TEST_SUITE_P(
    ConfigCheckTest, ConfigCheckTest,
    zxtest::Values(
        ConfigCheckTestParam{
            .hash_function = fuchsia_hardware_block_verified::wire::HashFunction::kSha256,
            .block_size = fuchsia_hardware_block_verified::wire::BlockSize::kSize4096,
            .block =
                {
                    .block_size = 4096,
                },
            .status = ZX_OK,
        },
        ConfigCheckTestParam{
            .hash_function = fuchsia_hardware_block_verified::wire::HashFunction::kSha256,
            .block_size = fuchsia_hardware_block_verified::wire::BlockSize::kSize4096,
            .block =
                {
                    .block_size = 512,
                },
            .status = ZX_OK,
        },
        ConfigCheckTestParam{
            .block_size = fuchsia_hardware_block_verified::wire::BlockSize::kSize4096,
            .block =
                {
                    .block_size = 4096,
                },
            .status = ZX_ERR_INVALID_ARGS,
        },
        ConfigCheckTestParam{
            .hash_function = fuchsia_hardware_block_verified::wire::HashFunction::kSha256,
            .block =
                {
                    .block_size = 4096,
                },
            .status = ZX_ERR_INVALID_ARGS,
        },
        ConfigCheckTestParam{
            .hash_function = fuchsia_hardware_block_verified::wire::HashFunction::kSha256,
            .block_size = fuchsia_hardware_block_verified::wire::BlockSize::kSize4096,
            .block =
                {
                    .block_size = 640,
                },
            .status = ZX_ERR_INVALID_ARGS,
        },
        ConfigCheckTestParam{
            .hash_function = fuchsia_hardware_block_verified::wire::HashFunction::kSha256,
            .block_size = fuchsia_hardware_block_verified::wire::BlockSize::kSize4096,
            .block =
                {
                    .block_size = 8192,
                },
            .status = ZX_ERR_INVALID_ARGS,
        }));

}  // namespace
