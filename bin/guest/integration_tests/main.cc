// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gmock/gmock.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>

#include "guest_test.h"

using ::testing::HasSubstr;

static constexpr char kVirtioRngUtilCmx[] = "meta/virtio_rng_test_util.cmx";

class ZirconGuestTest : public GuestTest<ZirconGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForSystemReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for system ready";
      return false;
    }
    return true;
  }
};

TEST_F(ZirconGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

TEST_F(ZirconGuestTest, VirtioRng) {
  std::string result;
  EXPECT_EQ(Run(kVirtioRngUtilCmx, "", &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

class ZirconMultiprocessorGuestTest
    : public GuestTest<ZirconMultiprocessorGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    return true;
  }
};

TEST_F(ZirconMultiprocessorGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class LinuxGuestTest : public GuestTest<LinuxGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kLinuxGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        "--cmdline=loglevel=0 console=hvc0 root=/dev/vda rw");
    return true;
  }
};

TEST_F(LinuxGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class LinuxMultiprocessorGuestTest
    : public GuestTest<LinuxMultiprocessorGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kLinuxGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back(
        "--cmdline=loglevel=0 console=hvc0 root=/dev/vda rw");
    return true;
  }
};

TEST_F(LinuxMultiprocessorGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}
