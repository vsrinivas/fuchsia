// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <sstream>
#include <string>

#include <fbl/unique_fd.h>
#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fxl/logging.h>
#include <lib/zx/socket.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/integration_tests/test_serial.h"

static constexpr char kGuestMgrUrl[] = "guestmgr";
static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kLinuxGuestUrl[] = "linux_guest";
static constexpr char kRealm[] = "realmguestintegrationtest";

class GuestTest : public component::testing::TestWithEnvironment {
 protected:
  void StartGuest(uint8_t num_cpus) {
    enclosing_environment_ = CreateNewEnclosingEnvironment(kRealm);
    ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment_.get()));

    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kGuestMgrUrl;
    ASSERT_EQ(ZX_OK, enclosing_environment_->AddServiceWithLaunchInfo(
                         std::move(launch_info),
                         fuchsia::guest::EnvironmentManager::Name_));

    fuchsia::guest::LaunchInfo guest_launch_info = LaunchInfo();

    std::stringstream cpu_arg;
    cpu_arg << "--cpus=" << std::to_string(num_cpus);
    guest_launch_info.args.push_back(cpu_arg.str());

    enclosing_environment_->ConnectToService(guest_mgr_.NewRequest());
    ASSERT_TRUE(guest_mgr_);
    guest_mgr_->Create(guest_launch_info.url, guest_env_.NewRequest());
    ASSERT_TRUE(guest_env_);

    guest_env_->LaunchInstance(std::move(guest_launch_info),
                               guest_controller_.NewRequest(),
                               [](fuchsia::guest::InstanceInfo) {});
    ASSERT_TRUE(guest_controller_);

    zx::socket socket;
    guest_controller_->GetSerial(
        [&socket](zx::socket s) { socket = std::move(s); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return socket.is_valid(); },
                                          zx::sec(5)));
    ASSERT_EQ(serial_.Start(std::move(socket)), ZX_OK);
  }

  zx_status_t Execute(std::string message, std::string* result = nullptr) {
    return serial_.ExecuteBlocking(message, result);
  }

  virtual fuchsia::guest::LaunchInfo LaunchInfo() = 0;

  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::guest::EnvironmentManagerPtr guest_mgr_;
  fuchsia::guest::EnvironmentControllerPtr guest_env_;
  fuchsia::guest::InstanceControllerPtr guest_controller_;
  TestSerial serial_;
};

class ZirconGuestTest : public GuestTest {
 protected:
  fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--network=false");
    return launch_info;
  }
};

TEST_F(ZirconGuestTest, LaunchGuest) {
  StartGuest(1);
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
  QuitLoop();
}

TEST_F(ZirconGuestTest, LaunchGuestMultiprocessor) {
  StartGuest(4);
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
  QuitLoop();
}

class LinuxGuestTest : public GuestTest {
 protected:
  fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kLinuxGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--network=false");
    launch_info.args.push_back("--cmdline-append=loglevel=0 console=hvc0");
    return launch_info;
  }
};

TEST_F(LinuxGuestTest, LaunchGuest) {
  StartGuest(1);
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
  QuitLoop();
}

TEST_F(LinuxGuestTest, LaunchGuestMultiprocessor) {
  StartGuest(4);
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
  QuitLoop();
}

zx_status_t hypervisor_supported() {
  fbl::unique_fd fd(open("/dev/misc/sysinfo", O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }
  zx::resource resource;
  ssize_t n = ioctl_sysinfo_get_hypervisor_resource(
      fd.get(), resource.reset_and_get_address());
  if (n < 0) {
    return ZX_ERR_IO;
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
