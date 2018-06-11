// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/environment_services.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fxl/logging.h>
#include <lib/zx/socket.h>
#include <zircon/device/sysinfo.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/integration_test/test_serial.h"

static constexpr char kGuestMgrUrl[] = "guestmgr";
static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kRealm[] = "realmguestintegrationtest";

class GuestTest : public component::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    enclosing_environment_ = CreateNewEnclosingEnvironment(kRealm);
    ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment_.get()));

    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kGuestMgrUrl;
    ASSERT_EQ(ZX_OK,
              enclosing_environment_->AddServiceWithLaunchInfo(
                  std::move(launch_info), fuchsia::guest::GuestManager::Name_));

    enclosing_environment_->ConnectToService(guest_mgr_.NewRequest());
    ASSERT_TRUE(guest_mgr_);
    guest_mgr_->CreateEnvironment(kZirconGuestUrl, guest_env_.NewRequest());
    ASSERT_TRUE(guest_env_);

    fuchsia::guest::GuestLaunchInfo guest_launch_info;
    guest_launch_info.url = GuestUrl();
    guest_launch_info.vmm_args.push_back("--display=none");
    guest_launch_info.vmm_args.push_back("--cpus=1");
    guest_launch_info.vmm_args.push_back("--network=false");
    guest_env_->LaunchGuest(std::move(guest_launch_info),
                            guest_controller_.NewRequest(),
                            [](fuchsia::guest::GuestInfo) {});
    ASSERT_TRUE(guest_controller_);

    zx::socket socket;
    guest_controller_->GetSerial(
        [&socket](zx::socket s) { socket = std::move(s); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return socket.is_valid(); },
                                          zx::sec(5)));
    serial_.Start(std::move(socket));
  }

  zx_status_t Execute(std::string message, std::string* result = nullptr) {
    return serial_.ExecuteBlocking(message, result);
  }

  virtual std::string GuestUrl() = 0;

  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::guest::GuestManagerPtr guest_mgr_;
  fuchsia::guest::GuestEnvironmentPtr guest_env_;
  fuchsia::guest::GuestControllerPtr guest_controller_;
  TestSerial serial_;
};

class ZirconGuestTest : public GuestTest {
 protected:
  std::string GuestUrl() { return kZirconGuestUrl; }
};

TEST_F(ZirconGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
  QuitLoop();
}

zx_status_t hypervisor_supported() {
  zx::vmo vmo;
  size_t vmo_size = PAGE_SIZE;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd < 0) {
    return ZX_ERR_IO;
  }
  zx_handle_t h;
  ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd, &h);
  close(fd);
  if (n < 0) {
    return ZX_ERR_IO;
  }
  zx::resource resource(std::move(h));
  zx::guest guest;
  return zx::guest::create(resource, 0, vmo, &guest);
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
