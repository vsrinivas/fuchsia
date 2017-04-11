// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_server_config.h"

#include "gtest/gtest.h"
#include "lib/ftl/functional/auto_call.h"

#define MULTILINE(...) #__VA_ARGS__
#define INVALID_CONFIGS 4

namespace timeservice {

const char* invalid_configs[INVALID_CONFIGS] = {
    MULTILINE({
      "servers" : [ {
        "name" : "Google",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2"
            "addresses" : [ {"address" : "address:7898"} ]
      } ]
    }),
    MULTILINE({
      "servers" : [ {
        "name" : "Google",
        "publicKey" : "3b6a27bcceb6a42d62a3a8d02a6f0d736434315771de243a63ac048a"
                      "18b59da29"
      } ]
    }),
    MULTILINE({
      "servers" : [ {
        "name" : "Google",
        "publicKey" : "3b6a27bcceb6a42d62a3a8d02a6f0d7365433577",
        "addresses" : [ {"address" : "address:7898"} ]
      } ]
    }),
    MULTILINE({})};

TEST(TimeServerConfigTest, HandlesInvalidInput) {
  for (int i = 0; i < INVALID_CONFIGS; i++) {
    char filename[] = "/tmp/ts_test.XXXXXX";
    int fd = mkstemp(filename);
    ASSERT_NE(fd, -1) << "Can't create temp file";
    auto ac1 = ftl::MakeAutoCall([&]() { unlink(filename); });
    write(fd, invalid_configs[i], strlen(invalid_configs[i]));
    close(fd);

    TimeServerConfig config;
    ASSERT_EQ(config.Parse(filename), false);
  }
}

TEST(TimeServerConfigTest, HandlesValidInput) {
  const char* json = MULTILINE({
    "servers" : [ {
      "name" : "Google",
      "publicKey" :
          "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
      "addresses" : [ {"address" : "address:7898"} ]
    } ]
  });
  char filename[] = "/tmp/ts_test.XXXXXX";
  int fd = mkstemp(filename);
  ASSERT_NE(fd, -1) << "Can't create temp file";
  auto ac1 = ftl::MakeAutoCall([&]() { unlink(filename); });
  write(fd, json, strlen(json));
  close(fd);

  TimeServerConfig config;
  ASSERT_EQ(config.Parse(filename), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 1u);
}

TEST(TimeServerConfigTest, HandlesMultipleAddressesInput) {
  const char* json = MULTILINE({
    "servers" : [ {
      "name" : "Google",
      "publicKey" :
          "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
      "addresses" :
          [ {"address" : "address:7898"}, {"address" : "address2:7898"} ]
    } ]
  });
  char filename[] = "/tmp/ts_test.XXXXXX";
  int fd = mkstemp(filename);
  ASSERT_NE(fd, -1) << "Can't create temp file";
  auto ac1 = ftl::MakeAutoCall([&]() { unlink(filename); });
  write(fd, json, strlen(json));
  close(fd);

  TimeServerConfig config;
  ASSERT_EQ(config.Parse(filename), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 2u);
}

TEST(TimeServerConfigTest, HandlesMultipleServerInput) {
  const char* json = MULTILINE({
    "servers" : [
      {
        "name" : "Google",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
        "addresses" : [ {"address" : "address:7898"} ]
      },
      {
        "name" : "Google2",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
        "addresses" : [ {"address" : "address:7898"} ]
      }
    ]
  });
  char filename[] = "/tmp/ts_test.XXXXXX";
  int fd = mkstemp(filename);
  ASSERT_NE(fd, -1) << "Can't create temp file";
  auto ac1 = ftl::MakeAutoCall([&]() { unlink(filename); });
  write(fd, json, strlen(json));
  close(fd);

  TimeServerConfig config;
  ASSERT_EQ(config.Parse(filename), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 2u);
}

TEST(TimeServerConfigTest, HandlesMultipleServerNAddressesInput) {
  const char* json = MULTILINE({
    "servers" : [
      {
        "name" : "Google",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
        "addresses" :
            [ {"address" : "address:7898"}, {"address" : "address2:7898"} ]
      },
      {
        "name" : "Google2",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
        "addresses" : [ {"address" : "address:7898"} ]
      }
    ]
  });
  char filename[] = "/tmp/ts_test.XXXXXX";
  int fd = mkstemp(filename);
  ASSERT_NE(fd, -1) << "Can't create temp file";
  auto ac1 = ftl::MakeAutoCall([&]() { unlink(filename); });
  write(fd, json, strlen(json));
  close(fd);

  TimeServerConfig config;
  ASSERT_EQ(config.Parse(filename), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 3u);
}

}  // namespace timeservice
