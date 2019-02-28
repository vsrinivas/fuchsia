// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/time_server_config.h"
#include "src/lib/files/scoped_temp_dir.h"

#include "gtest/gtest.h"

#define INVALID_CONFIGS 4

namespace time_server {

using files::ScopedTempDir;

const std::string invalid_configs[INVALID_CONFIGS] = {
    R"({
      "servers" : [ {
        "name" : "Google",
        "publicKey" :
            "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2"
            "addresses" : [ {"address" : "address:7898"} ]
      } ]
    })",
    R"({
      "servers" : [ {
        "name" : "Google",
        "publicKey" : "3b6a27bcceb6a42d62a3a8d02a6f0d736434315771de243a63ac048a"
                      "18b59da29"
      } ]
    })",
    R"({
      "servers" : [ {
        "name" : "Google",
        "publicKey" : "3b6a27bcceb6a42d62a3a8d02a6f0d7365433577",
        "addresses" : [ {"address" : "address:7898"} ]
      } ]
    })",
    "{}"};

TEST(TimeServerConfigTest, HandlesInvalidInput) {
  ScopedTempDir tmp_dir;
  for (auto& invalid_config : invalid_configs) {
    std::string config_path;
    tmp_dir.NewTempFileWithData(invalid_config, &config_path);
    TimeServerConfig config;
    ASSERT_EQ(config.Parse(config_path), false);
  }
}

TEST(TimeServerConfigTest, HandlesValidInput) {
  const std::string json = R"({
    "servers" : [ {
      "name" : "Google",
      "publicKey" :
          "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
      "addresses" : [ {"address" : "address:7898"} ]
    } ]
  })";
  ScopedTempDir tmp_dir;
  std::string config_path;
  tmp_dir.NewTempFileWithData(json, &config_path);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(config_path), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 1u);
}

TEST(TimeServerConfigTest, HandlesMultipleAddressesInput) {
  const std::string json = R"({
    "servers" : [ {
      "name" : "Google",
      "publicKey" :
          "3b6a27bcceb6a42d62a3a8d02a6f0d736343215771de243a63ac048a18b59da2",
      "addresses" :
          [ {"address" : "address:7898"}, {"address" : "address2:7898"} ]
    } ]
  })";
  ScopedTempDir tmp_dir;
  std::string config_path;
  tmp_dir.NewTempFileWithData(json, &config_path);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(config_path), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 2u);
}

TEST(TimeServerConfigTest, HandlesMultipleServerInput) {
  const std::string json = R"({
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
  })";
  ScopedTempDir tmp_dir;
  std::string config_path;
  tmp_dir.NewTempFileWithData(json, &config_path);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(config_path), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 2u);
}

TEST(TimeServerConfigTest, HandlesMultipleServerNAddressesInput) {
  const std::string json = R"({
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
  })";
  ScopedTempDir tmp_dir;
  std::string config_path;
  tmp_dir.NewTempFileWithData(json, &config_path);
  TimeServerConfig config;
  ASSERT_EQ(config.Parse(config_path), true);
  auto server_list = config.ServerList();
  ASSERT_EQ(server_list.size(), 3u);
}

}  // namespace time_server
