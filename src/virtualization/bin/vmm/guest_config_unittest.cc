// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest_config.h"

#include <zircon/compiler.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "guest.h"
#include "src/lib/fxl/arraysize.h"

class GuestConfigParserTest : public ::testing::Test {
 protected:
  fuchsia::virtualization::GuestConfig config_;

  zx_status_t ParseConfig(const std::string& config_str) {
    auto st = guest_config::ParseConfig(config_str, &config_);
    if (st == ZX_OK) {
      guest_config::SetDefaults(&config_);
    }
    return st;
  }

  zx_status_t ParseArgs(std::vector<const char*> args) {
    args.insert(args.begin(), "exe_name");
    return guest_config::ParseArguments(args.size(), args.data(), &config_);
  }
};

TEST_F(GuestConfigParserTest, DefaultValues) {
  ASSERT_EQ(ZX_OK, ParseConfig("{}"));
  ASSERT_FALSE(config_.has_kernel());
  ASSERT_FALSE(config_.has_kernel_path());
  ASSERT_FALSE(config_.has_ramdisk_path());
  ASSERT_EQ(zx_system_get_num_cpus(), config_.cpus());
  ASSERT_TRUE(config_.block_devices().empty());
  ASSERT_FALSE(config_.has_cmdline());
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
  ASSERT_EQ(fuchsia::virtualization::Kernel::ZIRCON, config_.kernel());
  ASSERT_EQ("zircon_path", config_.kernel_path());
  ASSERT_EQ("ramdisk_path", config_.ramdisk_path());
  ASSERT_EQ(4u, config_.cpus());
  ASSERT_EQ(1ul, config_.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config_.block_devices()[0].path);
  ASSERT_EQ("kernel cmdline", config_.cmdline());
}

TEST_F(GuestConfigParserTest, ParseDisallowedArgs) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--linux=linux_path"}));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--ramdisk=ramdisk_path"}));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--block=/pkg/data/block_path"}));
}

TEST_F(GuestConfigParserTest, ParseArgs) {
  std::string cpus = "--cpus=" + std::to_string(Guest::kMaxVcpus);
  ASSERT_EQ(ZX_OK, ParseArgs({cpus.c_str(), "--cmdline=kernel_cmdline"}));
  ASSERT_EQ(Guest::kMaxVcpus, config_.cpus());
  ASSERT_EQ("kernel_cmdline", config_.cmdline());
}

TEST_F(GuestConfigParserTest, InvalidCpusArgs) {
  std::string cpus = "--cpus=" + std::to_string(Guest::kMaxVcpus + 1);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({cpus.c_str(), "--cmdline=kernel_cmdline"}));
}

TEST_F(GuestConfigParserTest, UnknownArgument) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--invalid-arg"}));
}

TEST_F(GuestConfigParserTest, BooleanFlag) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--virtio-balloon=false"}));
  ASSERT_FALSE(config_.virtio_balloon());

  config_.clear_virtio_balloon();
  ASSERT_EQ(ZX_OK, ParseArgs({"--virtio-balloon=true"}));
  ASSERT_TRUE(config_.virtio_balloon());
}

TEST_F(GuestConfigParserTest, CommandLineAppend) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--cmdline=foo bar", "--cmdline-add=baz"}));
  guest_config::SetDefaults(&config_);
  ASSERT_EQ("foo bar baz", config_.cmdline());
}

TEST_F(GuestConfigParserTest, BlockSpecJson) {
  ASSERT_EQ(ZX_OK, ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,fdio",
            "/dev/class/block/001,rw,fdio"
          ]
        })JSON"));
  ASSERT_EQ(2ul, config_.block_devices().size());

  const fuchsia::virtualization::BlockSpec& spec0 = config_.block_devices()[0];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec0.format);
  ASSERT_EQ("/pkg/data/foo", spec0.path);

  const fuchsia::virtualization::BlockSpec& spec1 = config_.block_devices()[1];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec1.format);
  ASSERT_EQ("/dev/class/block/001", spec1.path);
}

TEST_F(GuestConfigParserTest, NetSpecArg) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--net=00:11:22:33:44:55", "--net=66:77:88:99:aa:bb"}));
  ASSERT_EQ(2ul, config_.net_devices().size());

  const fuchsia::virtualization::NetSpec& spec0 = config_.net_devices()[0];
  EXPECT_THAT(spec0.mac_address.octets, testing::ElementsAre(0x00, 0x11, 0x22, 0x33, 0x44, 0x55));

  const fuchsia::virtualization::NetSpec& spec1 = config_.net_devices()[1];
  EXPECT_THAT(spec1.mac_address.octets, testing::ElementsAre(0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--net=000:111:22:33:44:55"}));
}

TEST_F(GuestConfigParserTest, InterruptSpecArg) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--interrupt=32", "--interrupt=33"}));
  ASSERT_EQ(2ul, config_.interrupts().size());

  ASSERT_EQ(32u, config_.interrupts()[0]);
  ASSERT_EQ(33u, config_.interrupts()[1]);
}

TEST_F(GuestConfigParserTest, InterruptSpecJson) {
  ASSERT_EQ(ZX_OK, ParseConfig(
                       R"JSON({
          "interrupt": [
            "32",
            "33"
          ]
        })JSON"));
  ASSERT_EQ(2ul, config_.interrupts().size());

  const uint32_t& spec0 = config_.interrupts()[0];
  ASSERT_EQ(32u, spec0);

  const uint32_t& spec1 = config_.interrupts()[1];
  ASSERT_EQ(33u, spec1);
}

TEST_F(GuestConfigParserTest, Memory_1024k) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=1024k"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(1ul << 20, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_2M) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=2M"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(2ul << 20, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_4G) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=4G"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(4ul << 30, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_AddressAndSize) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=ffff,4G"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xfffful, memory[0].base);
  EXPECT_EQ(4ul << 30, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_HostCached) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=eeee,2G,cached"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xeeeeul, memory[0].base);
  EXPECT_EQ(2ul << 30, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::HOST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_HostDevice) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=dddd,1G,device"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xddddul, memory[0].base);
  EXPECT_EQ(1ul << 30, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::HOST_DEVICE, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_MultipleEntries) {
  ASSERT_EQ(ZX_OK, ParseArgs({"--memory=f0000000,1M", "--memory=ffffffff,2M"}));
  const auto& memory = config_.memory();
  EXPECT_EQ(2ul, memory.size());
  EXPECT_EQ(0xf0000000ul, memory[0].base);
  EXPECT_EQ(1ul << 20, memory[0].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[0].policy);
  EXPECT_EQ(0xfffffffful, memory[1].base);
  EXPECT_EQ(2ul << 20, memory[1].size);
  EXPECT_EQ(fuchsia::virtualization::MemoryPolicy::GUEST_CACHED, memory[1].policy);
}

TEST_F(GuestConfigParserTest, Memory_IllegalModifier) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--memory=5l"}));
}

TEST_F(GuestConfigParserTest, Memory_NonNumber) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArgs({"--memory=abc"}));
}

TEST_F(GuestConfigParserTest, VirtioGpu) {
  fuchsia::virtualization::GuestConfig config;

  ASSERT_EQ(ZX_OK, ParseArgs({"--virtio-gpu=true"}));
  ASSERT_TRUE(config_.virtio_gpu());

  config_.clear_virtio_gpu();
  ASSERT_EQ(ZX_OK, ParseArgs({"--virtio-gpu=false"}));
  ASSERT_FALSE(config_.virtio_gpu());
}
