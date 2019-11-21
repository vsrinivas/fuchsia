// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest_config.h"

#include <zircon/compiler.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/arraysize.h"

class GuestConfigParserTest : public ::testing::Test {
 protected:
  GuestConfig config_;
  GuestConfigParser parser_{&config_};

  zx_status_t ParseArg(const char* arg) {
    const char* argv[] = {"exe_name", arg};
    return parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv));
  }
};

TEST_F(GuestConfigParserTest, DefaultValues) {
  ASSERT_EQ(ZX_OK, parser_.ParseConfig("{}"));
  ASSERT_EQ(Kernel::ZIRCON, config_.kernel());
  ASSERT_TRUE(config_.kernel_path().empty());
  ASSERT_TRUE(config_.ramdisk_path().empty());
  ASSERT_EQ(zx_system_get_num_cpus(), config_.cpus());
  ASSERT_TRUE(config_.block_devices().empty());
  ASSERT_TRUE(config_.cmdline().empty());
}

TEST_F(GuestConfigParserTest, ParseConfig) {
  ASSERT_EQ(ZX_OK, parser_.ParseConfig(
                       R"JSON({
          "zircon": "zircon_path",
          "ramdisk": "ramdisk_path",
          "cpus": "4",
          "block": "/pkg/data/block_path",
          "cmdline": "kernel cmdline"
        })JSON"));
  ASSERT_EQ(Kernel::ZIRCON, config_.kernel());
  ASSERT_EQ("zircon_path", config_.kernel_path());
  ASSERT_EQ("ramdisk_path", config_.ramdisk_path());
  ASSERT_EQ(4u, config_.cpus());
  ASSERT_EQ(1ul, config_.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config_.block_devices()[0].path);
  ASSERT_EQ("kernel cmdline", config_.cmdline());
}

TEST_F(GuestConfigParserTest, ParseArgs) {
  const char* argv[] = {"exe_name", "--linux=linux_path",           "--ramdisk=ramdisk_path",
                        "--cpus=4", "--block=/pkg/data/block_path", "--cmdline=kernel_cmdline"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(Kernel::LINUX, config_.kernel());
  ASSERT_EQ("linux_path", config_.kernel_path());
  ASSERT_EQ("ramdisk_path", config_.ramdisk_path());
  ASSERT_EQ(4u, config_.cpus());
  ASSERT_EQ(1ul, config_.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config_.block_devices()[0].path);
  ASSERT_EQ("kernel_cmdline", config_.cmdline());
}

TEST_F(GuestConfigParserTest, UnknownArgument) {
  const char* argv[] = {"exe_name", "--invalid-arg"};
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
}

TEST_F(GuestConfigParserTest, BooleanFlag) {
  const char* argv_false[] = {"exe_name", "--virtio-balloon=false"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv_false), const_cast<char**>(argv_false)));
  ASSERT_FALSE(config_.virtio_balloon());

  const char* argv_true[] = {"exe_name", "--virtio-balloon=true"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv_true), const_cast<char**>(argv_true)));
  ASSERT_TRUE(config_.virtio_balloon());
}

TEST_F(GuestConfigParserTest, CommandLineAppend) {
  const char* argv[] = {"exe_name", "--cmdline=foo bar", "--cmdline-add=baz"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ("foo bar baz", config_.cmdline());
}

TEST_F(GuestConfigParserTest, BlockSpecArg) {
  const char* argv[] = {"exe_name", "--block=/pkg/data/foo,ro,fdio",
                        "--block=/dev/class/block/001,rw,fdio"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2ul, config_.block_devices().size());

  const BlockSpec& spec0 = config_.block_devices()[0];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec0.format);
  ASSERT_EQ("/pkg/data/foo", spec0.path);

  const BlockSpec& spec1 = config_.block_devices()[1];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec1.format);
  ASSERT_EQ("/dev/class/block/001", spec1.path);
}

TEST_F(GuestConfigParserTest, BlockSpecJson) {
  ASSERT_EQ(ZX_OK, parser_.ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,fdio",
            "/dev/class/block/001,rw,fdio"
          ]
        })JSON"));
  ASSERT_EQ(2ul, config_.block_devices().size());

  const BlockSpec& spec0 = config_.block_devices()[0];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec0.format);
  ASSERT_EQ("/pkg/data/foo", spec0.path);

  const BlockSpec& spec1 = config_.block_devices()[1];
  ASSERT_EQ(fuchsia::virtualization::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::virtualization::BlockFormat::RAW, spec1.format);
  ASSERT_EQ("/dev/class/block/001", spec1.path);
}

TEST_F(GuestConfigParserTest, NetSpecArg) {
  const char* argv[] = {"exe_name", "--net=00:11:22:33:44:55", "--net=66:77:88:99:aa:bb"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2ul, config_.net_devices().size());

  const NetSpec& spec0 = config_.net_devices()[0];
  EXPECT_THAT(spec0.mac_address.octets, testing::ElementsAre(0x00, 0x11, 0x22, 0x33, 0x44, 0x55));

  const NetSpec& spec1 = config_.net_devices()[1];
  EXPECT_THAT(spec1.mac_address.octets, testing::ElementsAre(0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb));

  const char* argv_invalid[] = {"exe_name", "--net=000:111:22:33:44:55"};
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            parser_.ParseArgcArgv(arraysize(argv_invalid), const_cast<char**>(argv_invalid)));
}

TEST_F(GuestConfigParserTest, InterruptSpecArg) {
  const char* argv[] = {"exe_name", "--interrupt=32", "--interrupt=33"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2ul, config_.interrupts().size());

  const uint32_t& spec0 = config_.interrupts()[0];
  ASSERT_EQ(32u, spec0);

  const uint32_t& spec1 = config_.interrupts()[1];
  ASSERT_EQ(33u, spec1);
}

TEST_F(GuestConfigParserTest, InterruptSpecJson) {
  ASSERT_EQ(ZX_OK, parser_.ParseConfig(
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
  ASSERT_EQ(ZX_OK, ParseArg("--memory=1024k"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(1ul << 20, memory[0].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_2M) {
  ASSERT_EQ(ZX_OK, ParseArg("--memory=2M"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(2ul << 20, memory[0].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_4G) {
  ASSERT_EQ(ZX_OK, ParseArg("--memory=4G"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0ul, memory[0].base);
  EXPECT_EQ(4ul << 30, memory[0].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_AddressAndSize) {
  ASSERT_EQ(ZX_OK, ParseArg("--memory=ffff,4G"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xfffful, memory[0].base);
  EXPECT_EQ(4ul << 30, memory[0].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_HostCached) {
  ASSERT_EQ(ZX_OK, ParseArg("--memory=eeee,2G,cached"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xeeeeul, memory[0].base);
  EXPECT_EQ(2ul << 30, memory[0].size);
  EXPECT_EQ(MemoryPolicy::HOST_CACHED, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_HostDevice) {
  ASSERT_EQ(ZX_OK, ParseArg("--memory=dddd,1G,device"));
  const auto& memory = config_.memory();
  EXPECT_EQ(1ul, memory.size());
  EXPECT_EQ(0xddddul, memory[0].base);
  EXPECT_EQ(1ul << 30, memory[0].size);
  EXPECT_EQ(MemoryPolicy::HOST_DEVICE, memory[0].policy);
}

TEST_F(GuestConfigParserTest, Memory_MultipleEntries) {
  const char* argv[] = {"exe_name", "--memory=f0000000,1M", "--memory=ffffffff,2M"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  const auto& memory = config_.memory();
  EXPECT_EQ(2ul, memory.size());
  EXPECT_EQ(0xf0000000ul, memory[0].base);
  EXPECT_EQ(1ul << 20, memory[0].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[0].policy);
  EXPECT_EQ(0xfffffffful, memory[1].base);
  EXPECT_EQ(2ul << 20, memory[1].size);
  EXPECT_EQ(MemoryPolicy::GUEST_CACHED, memory[1].policy);
}

TEST_F(GuestConfigParserTest, Memory_IllegalModifier) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArg("--memory=5l"));
}

TEST_F(GuestConfigParserTest, Memory_NonNumber) {
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ParseArg("--memory=abc"));
}

TEST_F(GuestConfigParserTest, VirtioGpu) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* virtio_gpu_true_argv[] = {"exe_name", "--virtio-gpu=true"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(virtio_gpu_true_argv),
                                         const_cast<char**>(virtio_gpu_true_argv)));
  ASSERT_TRUE(config_.virtio_gpu());

  const char* virtio_gpu_false_argv[] = {"exe_name", "--virtio-gpu=false"};
  ASSERT_EQ(ZX_OK, parser_.ParseArgcArgv(arraysize(virtio_gpu_false_argv),
                                         const_cast<char**>(virtio_gpu_false_argv)));
  ASSERT_FALSE(config_.virtio_gpu());
}
