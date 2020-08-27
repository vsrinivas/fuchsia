// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/args.h"

#include <lib/fdio/spawn.h>
#include <lib/sync/completion.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>

#include <zxtest/zxtest.h>

namespace {
constexpr char kInterface[] = "/dev/whatever/whatever";
constexpr char kNodename[] = "some-four-word-name";
constexpr char kEthDir[] = "/dev";

TEST(ArgsTest, NetsvcNodenamePrintsAndExits) {
  const std::string path = "/pkg/bin/netsvc";
  const char* argv[] = {path.c_str(), "--nodename", nullptr};
  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

  ASSERT_OK(fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr, 0,
                           nullptr, process.reset_and_get_address(), err_msg),
            "%s", err_msg);

  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));
  zx_info_process_t proc_info;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_EQ(proc_info.return_code, 0);
}

TEST(ArgsTest, NetsvcNoneProvided) {
  int argc = 1;
  const char* argv[] = {"netsvc"};
  bool netboot = false;
  bool nodename = false;
  bool advertise = false;
  bool all_features = false;
  const char* interface = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &nodename,
                              &advertise, &all_features, &interface),
            0, "%s", error);
  ASSERT_FALSE(netboot);
  ASSERT_FALSE(nodename);
  ASSERT_FALSE(advertise);
  ASSERT_FALSE(all_features);
  ASSERT_EQ(interface, nullptr);
  ASSERT_EQ(error, nullptr);
}

TEST(ArgsTest, NetsvcAllProvided) {
  int argc = 7;
  const char* argv[] = {
      "netsvc",         "--netboot",   "--nodename", "--advertise",
      "--all-features", "--interface", kInterface,
  };
  bool netboot = false;
  bool nodename = false;
  bool advertise = false;
  bool all_features = false;
  const char* interface = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &nodename,
                              &advertise, &all_features, &interface),
            0, "%s", error);
  ASSERT_TRUE(netboot);
  ASSERT_TRUE(nodename);
  ASSERT_TRUE(advertise);
  ASSERT_TRUE(all_features);
  ASSERT_EQ(interface, kInterface);
  ASSERT_EQ(error, nullptr);
}

TEST(ArgsTest, NetsvcValidation) {
  int argc = 2;
  const char* argv[] = {
      "netsvc",
      "--interface",
  };
  bool netboot = false;
  bool nodename = false;
  bool advertise = false;
  bool all_features = false;
  const char* interface = nullptr;
  const char* error = nullptr;
  ASSERT_LT(parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &nodename,
                              &advertise, &all_features, &interface),
            0);
  ASSERT_EQ(interface, nullptr);
  ASSERT_TRUE(strstr(error, "interface"));
}

TEST(ArgsTest, DeviceNameProviderNoneProvided) {
  int argc = 1;
  const char* argv[] = {"device-name-provider"};
  const char* interface = nullptr;
  const char* nodename = nullptr;
  const char* ethdir = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(parse_device_name_provider_args(argc, const_cast<char**>(argv), &error, &interface,
                                            &nodename, &ethdir),
            0, "%s", error);
  ASSERT_EQ(interface, nullptr);
  ASSERT_EQ(nodename, nullptr);
  ASSERT_EQ(ethdir, nullptr);
  ASSERT_EQ(error, nullptr);
}

TEST(ArgsTest, DeviceNameProviderAllProvided) {
  int argc = 7;
  const char* argv[] = {"device-name-provider",
                        "--nodename",
                        kNodename,
                        "--interface",
                        kInterface,
                        "--ethdir",
                        kEthDir};
  const char* interface = nullptr;
  const char* nodename = nullptr;
  const char* ethdir = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(parse_device_name_provider_args(argc, const_cast<char**>(argv), &error, &interface,
                                            &nodename, &ethdir),
            0, "%s", error);
  ASSERT_EQ(interface, kInterface);
  ASSERT_EQ(nodename, kNodename);
  ASSERT_EQ(ethdir, kEthDir);
  ASSERT_EQ(error, nullptr);
}

TEST(ArgsTest, DeviceNameProviderValidation) {
  int argc = 2;
  const char* argv[] = {
      "device-name-provider",
      "--interface",
  };
  const char* interface = nullptr;
  const char* nodename = nullptr;
  const char* ethdir = nullptr;
  const char* error = nullptr;
  ASSERT_LT(parse_device_name_provider_args(argc, const_cast<char**>(argv), &error, &interface,
                                            &nodename, &ethdir),
            0);
  ASSERT_EQ(interface, nullptr);
  ASSERT_TRUE(strstr(error, "interface"));

  argc = 2;
  argv[1] = "--nodename";
  interface = nullptr;
  nodename = nullptr;
  error = nullptr;
  ASSERT_LT(parse_device_name_provider_args(argc, const_cast<char**>(argv), &error, &interface,
                                            &nodename, &ethdir),
            0);
  ASSERT_EQ(nodename, nullptr);
  ASSERT_TRUE(strstr(error, "nodename"));
}
}  // namespace
