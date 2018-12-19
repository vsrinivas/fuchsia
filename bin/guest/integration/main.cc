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
static constexpr char kLinuxTestUtilDir[] = "/testutils";

class ZirconSingleCpuGuestTest : public GuestTest<ZirconSingleCpuGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr";
      return false;
    }
    return true;
  }
};

TEST_F(ZirconSingleCpuGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class ZirconGuestTest : public GuestTest<ZirconGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr";
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

class LinuxSingleCpuGuestTest : public GuestTest<LinuxSingleCpuGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kLinuxGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        "--cmdline=loglevel=0 console=hvc0 root=/dev/vda rw");
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForShellReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for shell";
      return false;
    }
    return true;
  }
};

TEST_F(LinuxSingleCpuGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class LinuxGuestTest : public GuestTest<LinuxGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kLinuxGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back(
        "--cmdline=loglevel=0 console=hvc0 root=/dev/vda rw");
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForShellReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for shell";
      return false;
    }
    return true;
  }
};

TEST_F(LinuxGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

TEST_F(LinuxGuestTest, VirtioRng) {
  std::string cmd =
      fxl::StringPrintf("%s/%s", kLinuxTestUtilDir, "virtio_rng_test_util");
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}