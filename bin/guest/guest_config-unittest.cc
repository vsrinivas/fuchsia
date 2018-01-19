// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_config.h"

#include <zircon/compiler.h>

#include "gtest/gtest.h"

namespace guest {
namespace {

TEST(GuestConfigParserTest, DefaultValues) {
  GuestConfig config;
  GuestConfigParser parser(&config);
  parser.ParseConfig("{}");

  ASSERT_STREQ("/pkg/data/kernel", config.kernel_path().c_str());
  ASSERT_STREQ("/pkg/data/ramdisk", config.ramdisk_path().c_str());
  ASSERT_TRUE(config.block_devices().empty());
  ASSERT_TRUE(config.cmdline().empty());
  ASSERT_EQ(0, config.balloon_interval());
  ASSERT_EQ(0, config.balloon_pages_threshold());
  ASSERT_FALSE(config.balloon_demand_page());
}

TEST(GuestConfigParserTest, ParseConfig) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "kernel": "kernel_path",
          "ramdisk": "ramdisk_path",
          "block": "/pkg/data/block_path",
          "cmdline": "kernel cmdline",
          "balloon-interval": "1234",
          "balloon-threshold": "5678",
          "balloon-demand-page": "true"
        })JSON"));
  ASSERT_STREQ("kernel_path", config.kernel_path().c_str());
  ASSERT_STREQ("ramdisk_path", config.ramdisk_path().c_str());
  ASSERT_EQ(1, config.block_devices().size());
  ASSERT_STREQ("/pkg/data/block_path", config.block_devices()[0].path.c_str());
  ASSERT_STREQ("kernel cmdline", config.cmdline().c_str());
  ASSERT_EQ(ZX_SEC(1234), config.balloon_interval());
  ASSERT_EQ(5678, config.balloon_pages_threshold());
  ASSERT_TRUE(config.balloon_demand_page());
}

TEST(GuestConfigParserTest, ParseArgs) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name",
                        "--kernel=kernel_path",
                        "--ramdisk=ramdisk_path",
                        "--block=/pkg/data/block_path",
                        "--cmdline=kernel_cmdline",
                        "--balloon-interval=1234",
                        "--balloon-threshold=5678",
                        "--balloon-demand-page"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(countof(argv), const_cast<char**>(argv)));
  ASSERT_STREQ("kernel_path", config.kernel_path().c_str());
  ASSERT_STREQ("ramdisk_path", config.ramdisk_path().c_str());
  ASSERT_EQ(1, config.block_devices().size());
  ASSERT_STREQ("/pkg/data/block_path", config.block_devices()[0].path.c_str());
  ASSERT_STREQ("kernel_cmdline", config.cmdline().c_str());
  ASSERT_EQ(ZX_SEC(1234), config.balloon_interval());
  ASSERT_EQ(5678, config.balloon_pages_threshold());
  ASSERT_TRUE(config.balloon_demand_page());
}

TEST(GuestConfigParserTest, UnknownArgument) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--invalid-arg"};
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            parser.ParseArgcArgv(countof(argv), const_cast<char**>(argv)));
}

TEST(GuestConfigParserTest, BooleanFlag) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv_false[] = {"exe_name", "--balloon-demand-page=false"};
  ASSERT_EQ(ZX_OK, parser.ParseArgcArgv(countof(argv_false),
                                        const_cast<char**>(argv_false)));
  ASSERT_FALSE(config.balloon_demand_page());

  const char* argv_true[] = {"exe_name", "--balloon-demand-page=true"};
  ASSERT_EQ(ZX_OK, parser.ParseArgcArgv(countof(argv_true),
                                        const_cast<char**>(argv_true)));
  ASSERT_TRUE(config.balloon_demand_page());
}

TEST(GuestConfigParserTest, BlockSpecArg) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  const char* argv[] = {"exe_name", "--block=/pkg/data/foo,ro,fdio",
                        "--block=/dev/class/block/001,rw,fifo"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(countof(argv), const_cast<char**>(argv)));
  ASSERT_EQ(2, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_FALSE(spec0.writable);
  ASSERT_EQ(BlockSpec::Mode::FDIO, spec0.mode);
  ASSERT_STREQ("/pkg/data/foo", spec0.path.c_str());

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_TRUE(spec1.writable);
  ASSERT_EQ(BlockSpec::Mode::FIFO, spec1.mode);
  ASSERT_STREQ("/dev/class/block/001", spec1.path.c_str());
}

TEST(GuestConfigParserTest, BlockSpecJson) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,fdio",
            "/dev/class/block/001,rw,fifo"
          ]
        })JSON"));
  ASSERT_EQ(2, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_FALSE(spec0.writable);
  ASSERT_EQ(BlockSpec::Mode::FDIO, spec0.mode);
  ASSERT_STREQ("/pkg/data/foo", spec0.path.c_str());

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_TRUE(spec1.writable);
  ASSERT_EQ(BlockSpec::Mode::FIFO, spec1.mode);
  ASSERT_STREQ("/dev/class/block/001", spec1.path.c_str());
}

}  // namespace
}  // namespace guest
