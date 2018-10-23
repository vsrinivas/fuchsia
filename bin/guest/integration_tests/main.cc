// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string>

#include <fbl/unique_fd.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fxl/logging.h>
#include <lib/zx/socket.h>
#include <zircon/syscalls/hypervisor.h>

#include "guest_test.h"

class ZirconGuestTest : public GuestTest<ZirconGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    return true;
  }
};

TEST_F(ZirconGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class ZirconMultiprocessorGuestTest
    : public GuestTest<ZirconMultiprocessorGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=4");
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
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cmdline-append=loglevel=0 console=hvc0");
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
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cmdline-append=loglevel=0 console=hvc0");
    return true;
  }
};

TEST_F(LinuxMultiprocessorGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

zx_status_t hypervisor_supported() {
  fbl::unique_fd fd(open("/dev/misc/sysinfo", O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx::resource resource;
  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetHypervisorResource(
      channel.get(), &status, resource.reset_and_get_address());
  if (fidl_status != ZX_OK) {
    return fidl_status;
  } else if (status != ZX_OK) {
    return status;
  }

  zx::guest guest;
  zx::vmar vmar;
  return zx::guest::create(resource, 0, &guest, &vmar);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  zx_status_t status = hypervisor_supported();
  if (status == ZX_ERR_NOT_SUPPORTED) {
    FXL_LOG(INFO) << "Hypervisor is not supported";
    return ZX_OK;
  } else if (status != ZX_OK) {
    return status;
  }

  return RUN_ALL_TESTS();
}
