// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/time_server_config.h"

#include "gtest/gtest.h"
#include "lib/fxl/functional/auto_call.h"

#define MULTILINE(...) #__VA_ARGS__
#define INVALID_CONFIGS 4

namespace {

class TempFile {
 public:
  TempFile(const char* contents) {
    int fd = mkstemp(pathname_);
    EXPECT_GE(fd, 0) << "Failed to create temp file";
    ssize_t len = strlen(contents);
    EXPECT_EQ(write(fd, contents, len), len);
    EXPECT_EQ(close(fd), 0);
  }
  ~TempFile() { EXPECT_EQ(unlink(pathname_), 0); }
  const char* pathname() { return pathname_; }

 private:
  char pathname_[29] = "/tmp/time_server_test_XXXXXX";
};

}  // namespace

namespace time_zone {

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
    TempFile temp_file(invalid_configs[i]);
    TimeServerConfig config;
    ASSERT_EQ(config.Parse(temp_file.pathname()), false);
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
  TempFile temp_file(json);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(temp_file.pathname()), true);
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
  TempFile temp_file(json);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(temp_file.pathname()), true);
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
  TempFile temp_file(json);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(temp_file.pathname()), true);
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
  TempFile temp_file(json);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(temp_file.pathname()), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 3u);
}

}  // namespace time_zone
