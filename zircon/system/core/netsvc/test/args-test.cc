// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../args.h"
#include <zxtest/zxtest.h>

namespace {
constexpr char kInterface[] = "/dev/whatever/whatever";
constexpr char kNodename[] = "some-four-word-name";
constexpr char kEthDir[] = "/dev";

TEST(ArgsTest, NetsvcNoneProvided) {
  int argc = 1;
  const char* argv[] = {"netsvc"};
  bool netboot = false;
  bool advertise = false;
  const char* interface = nullptr;
  const char* nodename = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(
      parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &advertise, &interface),
      0, "%s", error);
  ASSERT_FALSE(netboot);
  ASSERT_FALSE(advertise);
  ASSERT_EQ(interface, nullptr);
  ASSERT_EQ(nodename, nullptr);
  ASSERT_EQ(error, nullptr);
}

TEST(ArgsTest, NetsvcAllProvided) {
  int argc = 5;
  const char* argv[] = {
      "netsvc", "--netboot", "--advertise", "--interface", kInterface,
  };
  bool netboot = false;
  bool advertise = false;
  const char* interface = nullptr;
  const char* error = nullptr;
  ASSERT_EQ(
      parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &advertise, &interface),
      0, "%s", error);
  ASSERT_TRUE(netboot);
  ASSERT_TRUE(advertise);
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
  bool advertise = false;
  const char* interface = nullptr;
  const char* error = nullptr;
  ASSERT_LT(
      parse_netsvc_args(argc, const_cast<char**>(argv), &error, &netboot, &advertise, &interface),
      0);
  ASSERT_EQ(interface, nullptr);
  ASSERT_TRUE(strstr(error, "interface"));
}

TEST(ArgsTest, DeviceNameProviderNoneProvided) {
  int argc = 1;
  const char* argv[] = {"netsvc"};
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
      "netsvc",
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
