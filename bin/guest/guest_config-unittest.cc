// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_config.h"

#include <zircon/compiler.h>

#include "gtest/gtest.h"

namespace guest {
namespace {

#define TEST_GUID_STRING "14db42cf-beb7-46a2-9ef8-89b13bb80528"
static constexpr uint8_t TEST_GUID_VALUE[] = {
    // clang-format off
    0xcf, 0x42, 0xdb, 0x14,
    0xb7, 0xbe,
    0xa2, 0x46,
    0x9e, 0xf8, 0x89, 0xb1, 0x3b, 0xb8, 0x05, 0x28
    // clang-format on
};

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
  ASSERT_FALSE(config.block_wait());
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
          "balloon-demand-page": "true",
          "block-wait": "true"
        })JSON"));
  ASSERT_STREQ("kernel_path", config.kernel_path().c_str());
  ASSERT_STREQ("ramdisk_path", config.ramdisk_path().c_str());
  ASSERT_EQ(1, config.block_devices().size());
  ASSERT_STREQ("/pkg/data/block_path", config.block_devices()[0].path.c_str());
  ASSERT_STREQ("kernel cmdline", config.cmdline().c_str());
  ASSERT_EQ(ZX_SEC(1234), config.balloon_interval());
  ASSERT_EQ(5678, config.balloon_pages_threshold());
  ASSERT_TRUE(config.balloon_demand_page());
  ASSERT_TRUE(config.block_wait());
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
                        "--balloon-demand-page",
                        "--block-wait"};
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
  ASSERT_TRUE(config.block_wait());
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
                        "--block=/dev/class/block/001,rw,fifo",
                        "--block=guid:" TEST_GUID_STRING ",rw,fifo",
                        "--block=type-guid:" TEST_GUID_STRING ",ro,fdio"};
  ASSERT_EQ(ZX_OK,
            parser.ParseArgcArgv(countof(argv), const_cast<char**>(argv)));
  ASSERT_EQ(4, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RO, spec0.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FDIO, spec0.data_plane);
  ASSERT_STREQ("/pkg/data/foo", spec0.path.c_str());
  ASSERT_TRUE(spec0.guid.empty());

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RW, spec1.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FIFO, spec1.data_plane);
  ASSERT_STREQ("/dev/class/block/001", spec1.path.c_str());
  ASSERT_TRUE(spec1.guid.empty());

  const BlockSpec& spec2 = config.block_devices()[2];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RW, spec2.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FIFO, spec2.data_plane);
  ASSERT_TRUE(spec2.path.empty());
  ASSERT_EQ(machina::BlockDispatcher::GuidType::GPT_PARTITION_GUID,
            spec2.guid.type);
  ASSERT_EQ(0, memcmp(spec2.guid.bytes, TEST_GUID_VALUE, GUID_LEN));

  const BlockSpec& spec3 = config.block_devices()[3];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RO, spec3.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FDIO, spec3.data_plane);
  ASSERT_TRUE(spec3.path.empty());
  ASSERT_EQ(machina::BlockDispatcher::GuidType::GPT_PARTITION_TYPE_GUID,
            spec3.guid.type);
  ASSERT_EQ(0, memcmp(spec3.guid.bytes, TEST_GUID_VALUE, GUID_LEN));
}

TEST(GuestConfigParserTest, BlockSpecJson) {
  GuestConfig config;
  GuestConfigParser parser(&config);

  ASSERT_EQ(ZX_OK, parser.ParseConfig(
                       R"JSON({
          "block": [
            "/pkg/data/foo,ro,fdio",
            "/dev/class/block/001,rw,fifo",
            "guid:)JSON" TEST_GUID_STRING R"JSON(,rw,fifo",
            "type-guid:)JSON" TEST_GUID_STRING R"JSON(,ro,fdio"
          ]
        })JSON"));
  ASSERT_EQ(4, config.block_devices().size());

  const BlockSpec& spec0 = config.block_devices()[0];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RO, spec0.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FDIO, spec0.data_plane);
  ASSERT_STREQ("/pkg/data/foo", spec0.path.c_str());

  const BlockSpec& spec1 = config.block_devices()[1];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RW, spec1.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FIFO, spec1.data_plane);
  ASSERT_STREQ("/dev/class/block/001", spec1.path.c_str());

  const BlockSpec& spec2 = config.block_devices()[2];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RW, spec2.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FIFO, spec2.data_plane);
  ASSERT_TRUE(spec2.path.empty());
  ASSERT_EQ(machina::BlockDispatcher::GuidType::GPT_PARTITION_GUID,
            spec2.guid.type);
  ASSERT_EQ(0, memcmp(spec2.guid.bytes, TEST_GUID_VALUE, GUID_LEN));

  const BlockSpec& spec3 = config.block_devices()[3];
  ASSERT_EQ(machina::BlockDispatcher::Mode::RO, spec3.mode);
  ASSERT_EQ(machina::BlockDispatcher::DataPlane::FDIO, spec3.data_plane);
  ASSERT_TRUE(spec3.path.empty());
  ASSERT_EQ(machina::BlockDispatcher::GuidType::GPT_PARTITION_TYPE_GUID,
            spec3.guid.type);
  ASSERT_EQ(0, memcmp(spec3.guid.bytes, TEST_GUID_VALUE, GUID_LEN));
}

#define TEST_PARSE_GUID(name, guid, result)                                   \
  TEST(GuestConfigParserTest, GuidTest##name) {                               \
    GuestConfig config;                                                       \
    GuestConfigParser parser(&config);                                        \
                                                                              \
    const char* argv[] = {"exe_name", "--block=guid:" guid};                  \
    ASSERT_EQ((result),                                                       \
              parser.ParseArgcArgv(countof(argv), const_cast<char**>(argv))); \
  }

TEST_PARSE_GUID(LowerCase, "14db42cf-beb7-46a2-9ef8-89b13bb80528", ZX_OK);
TEST_PARSE_GUID(UpperCase, "14DB42CF-BEB7-46A2-9EF8-89B13BB80528", ZX_OK);
TEST_PARSE_GUID(MixedCase, "14DB42CF-BEB7-46A2-9ef8-89b13bb80528", ZX_OK);
TEST_PARSE_GUID(MissingDelimeters,
                "14db42cfbeb746a29ef889b13bb80528",
                ZX_ERR_INVALID_ARGS);
TEST_PARSE_GUID(ExtraDelimeters,
                "14-db-42cf-beb7-46-a2-9ef8-89b13bb80528",
                ZX_ERR_INVALID_ARGS);
TEST_PARSE_GUID(
    TooLong,
    "14db42cf-beb7-46a2-9ef8-89b13bb80528-14db42cf-beb7-46a2-9ef8-"
    "89b13bb80528-14db42cf-beb7-46a2-9ef8-89b13bb80528-14db42cf-beb7-"
    "46a2-9ef8-89b13bb80528-14db42cf-beb7-46a2-9ef8-89b13bb80528",
    ZX_ERR_INVALID_ARGS);
TEST_PARSE_GUID(TooShort, "14db42cf", ZX_ERR_INVALID_ARGS);
TEST_PARSE_GUID(IllegalCharacters,
                "abcdefgh-ijkl-mnop-qrst-uvwxyz!@#$%^",
                ZX_ERR_INVALID_ARGS);

}  // namespace
}  // namespace guest
