// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/guest_config.h"

#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include "gtest/gtest.h"
#include "lib/fxl/arraysize.h"

namespace guest {
namespace {

TEST(GuestConfigParserTest, DefaultValues) {
  GuestConfig config;
  GuestConfigParser parser(&config);
  parser.ParseConfig("{}");

  ASSERT_EQ(Kernel::ZIRCON, config.kernel());
  ASSERT_TRUE(config.kernel_path().empty());
  ASSERT_TRUE(config.ramdisk_path().empty());
  ASSERT_EQ(zx_system_get_num_cpus(), config.cpus());
  ASSERT_TRUE(config.block_devices().empty());
  ASSERT_TRUE(config.cmdline().empty());
}

TEST(GuestConfigParserTest, ParseConfig) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "zircon": "zircon_path",
          "ramdisk": "ramdisk_path",
          "cpus": "4",
          "block": "/pkg/data/block_path",
          "cmdline": "kernel cmdline"
        })JSON"));
  ASSERT_EQ(Kernel::ZIRCON, config.kernel());
  ASSERT_EQ("zircon_path", config.kernel_path());
  ASSERT_EQ("ramdisk_path", config.ramdisk_path());
  ASSERT_EQ(4, config.cpus());
  ASSERT_EQ(1, config.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config.block_devices()[0].path);
  ASSERT_EQ("kernel cmdline", config.cmdline());
}

TEST(GuestConfigParserTest, ParseArgs) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {
      "exe_name", "--linux=linux_path",           "--ramdisk=ramdisk_path",
      "--cpus=4", "--block=/pkg/data/block_path", "--cmdline=kernel_cmdline"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(Kernel::LINUX, config.kernel());
  ASSERT_EQ("linux_path", config.kernel_path());
  ASSERT_EQ("ramdisk_path", config.ramdisk_path());
  ASSERT_EQ(4, config.cpus());
  ASSERT_EQ(1, config.block_devices().size());
  ASSERT_EQ("/pkg/data/block_path", config.block_devices()[0].path);
  ASSERT_EQ("kernel_cmdline", config.cmdline());
}

TEST(GuestConfigParserTest, UnknownArgument) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--invalid-arg"};
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
}

TEST(GuestConfigParserTest, BooleanFlag) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv_false[] = {"exe_name", "--virtio-net=false"};
  ASSERT_EQ(ZX_OK, parser.ParseArgcArgv(arraysize(argv_false),
                                        const_cast<char**>(argv_false)));
  ASSERT_FALSE(config.virtio_net());

  const char* argv_true[] = {"exe_name", "--virtio-net=true"};
  ASSERT_EQ(ZX_OK, parser.ParseArgcArgv(arraysize(argv_true),
                                        const_cast<char**>(argv_true)));
  ASSERT_TRUE(config.virtio_net());
}

TEST(GuestConfigParserTest, CommandLineAppend) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--cmdline=foo bar", "--cmdline-add=baz"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ("foo bar baz", config.cmdline());
}

TEST(GuestConfigParserTest, BlockSpecArg) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--block=/pkg/data/foo,ro,fdio",
                        "--block=/dev/class/block/001,rw,fdio"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_EQ(fuchsia::guest::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::guest::BlockFormat::RAW, spec0.format);
  ASSERT_EQ("/pkg/data/foo", spec0.path);

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_EQ(fuchsia::guest::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::guest::BlockFormat::RAW, spec1.format);
  ASSERT_EQ("/dev/class/block/001", spec1.path);
}

TEST(GuestConfigParserTest, BlockSpecJson) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,fdio",
            "/dev/class/block/001,rw,fdio"
          ]
        })JSON"));
  ASSERT_EQ(2, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_EQ(fuchsia::guest::BlockMode::READ_ONLY, spec0.mode);
  ASSERT_EQ(fuchsia::guest::BlockFormat::RAW, spec0.format);
  ASSERT_EQ("/pkg/data/foo", spec0.path);

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_EQ(fuchsia::guest::BlockMode::READ_WRITE, spec1.mode);
  ASSERT_EQ(fuchsia::guest::BlockFormat::RAW, spec1.format);
  ASSERT_EQ("/dev/class/block/001", spec1.path);
}

TEST(GuestConfigParserTest, InterruptSpecArg) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--interrupt=32,2", "--interrupt=33,4"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2, config.interrupts().size());

  const InterruptSpec& spec0 = config.interrupts()[0];
  ASSERT_EQ(32, spec0.vector);
  ASSERT_EQ(ZX_INTERRUPT_MODE_EDGE_LOW, spec0.options);

  const InterruptSpec& spec1 = config.interrupts()[1];
  ASSERT_EQ(33, spec1.vector);
  ASSERT_EQ(ZX_INTERRUPT_MODE_EDGE_HIGH, spec1.options);
}

TEST(GuestConfigParserTest, InterruptSpecJson) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "interrupt": [
            "32,2",
            "33,4"
          ]
        })JSON"));
  ASSERT_EQ(2, config.interrupts().size());

  const InterruptSpec& spec0 = config.interrupts()[0];
  ASSERT_EQ(32, spec0.vector);
  ASSERT_EQ(ZX_INTERRUPT_MODE_EDGE_LOW, spec0.options);

  const InterruptSpec& spec1 = config.interrupts()[1];
  ASSERT_EQ(33, spec1.vector);
  ASSERT_EQ(ZX_INTERRUPT_MODE_EDGE_HIGH, spec1.options);
}

#define TEST_PARSE_MEM_SIZE(string, result)                           \
  TEST(GuestConfigParserTest, MemSizeTest_##string) {                 \
    GuestConfig config;                                               \
    GuestConfigParser parser(&config);                                \
                                                                      \
    const char* argv[] = {"exe_name", "--memory=" #string};           \
    ASSERT_EQ(ZX_OK, parser.ParseArgcArgv(arraysize(argv),            \
                                          const_cast<char**>(argv))); \
    ASSERT_EQ((result), config.memory());                             \
  }

TEST_PARSE_MEM_SIZE(1024k, 1u << 20);
TEST_PARSE_MEM_SIZE(2M, 2ul << 20);
TEST_PARSE_MEM_SIZE(4G, 4ul << 30);

#define TEST_PARSE_MEM_SIZE_ERROR(name, string)                           \
  TEST(GuestConfigParserTest, MemSizeTest_##name) {                       \
    GuestConfig config;                                                   \
    GuestConfigParser parser(&config);                                    \
                                                                          \
    const char* argv[] = {"exe_name", "--memory=" #string};               \
    ASSERT_EQ(                                                            \
        ZX_ERR_INVALID_ARGS,                                              \
        parser.ParseArgcArgv(arraysize(argv), const_cast<char**>(argv))); \
  }

TEST_PARSE_MEM_SIZE_ERROR(TooSmall, 1024);
TEST_PARSE_MEM_SIZE_ERROR(IllegalModifier, 5l);
TEST_PARSE_MEM_SIZE_ERROR(NonNumber, abc);

TEST(GuestConfigParserTest, VirtioGpu) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* virtio_gpu_true_argv[] = {"exe_name", "--virtio-gpu=true"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(virtio_gpu_true_argv),
                                 const_cast<char**>(virtio_gpu_true_argv)));
  ASSERT_TRUE(config.virtio_gpu());

  const char* virtio_gpu_false_argv[] = {"exe_name", "--virtio-gpu=false"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(arraysize(virtio_gpu_false_argv),
                                 const_cast<char**>(virtio_gpu_false_argv)));
  ASSERT_FALSE(config.virtio_gpu());
}

}  // namespace
}  // namespace guest
