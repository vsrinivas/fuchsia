// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <string>

#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <gmock/gmock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fdio/util.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/socket.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/integration_tests/test_serial.h"

using ::testing::HasSubstr;

static constexpr char kGuestMgrUrl[] = "guestmgr";
static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kLinuxGuestUrl[] = "linux_guest";
static constexpr char kRealm[] = "realmguestintegrationtest";
static constexpr char kTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";
static constexpr char kVirtioBlockUtilCmx[] = "meta/virtio_block_test_util.cmx";

// TODO(alexlegg): Use the block size from the virtio-block implementation
// directly.
static constexpr uint32_t kVirtioBlockSize = 512;
static constexpr uint32_t kVirtioBlockCount = 32;

class GuestTest : public component::testing::TestWithEnvironment {
 protected:
  void StartGuest(uint8_t num_cpus) {
    auto services = CreateServices();
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kGuestMgrUrl;
    zx_status_t status = services->AddServiceWithLaunchInfo(
        std::move(launch_info), fuchsia::guest::EnvironmentManager::Name_);
    ASSERT_EQ(ZX_OK, status);
    enclosing_environment_ =
        CreateNewEnclosingEnvironment(kRealm, std::move(services));
    bool started = WaitForEnclosingEnvToStart(enclosing_environment_.get());
    ASSERT_TRUE(started);

    fuchsia::guest::LaunchInfo guest_launch_info = LaunchInfo();
    guest_launch_info.args.push_back(fxl::StringPrintf("--cpus=%u", num_cpus));

    enclosing_environment_->ConnectToService(environment_manager_.NewRequest());
    environment_manager_->Create(guest_launch_info.url,
                                 environment_controller_.NewRequest());
    fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
    environment_controller_->LaunchInstance(
        std::move(guest_launch_info), view_provider.NewRequest(),
        instance_controller_.NewRequest(), [](...) {});

    zx::socket socket;
    instance_controller_->GetSerial(
        [&socket](zx::socket s) { socket = std::move(s); });
    bool is_valid = RunLoopWithTimeoutOrUntil(
        [&socket] { return socket.is_valid(); }, zx::sec(5));
    ASSERT_TRUE(is_valid);
    status = serial_.Start(std::move(socket));
    ASSERT_EQ(ZX_OK, status);
  }

  zx_status_t Execute(std::string message, std::string* result = nullptr) {
    return serial_.ExecuteBlocking(message, result);
  }

  virtual fuchsia::guest::LaunchInfo LaunchInfo() = 0;

  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::guest::EnvironmentManagerPtr environment_manager_;
  fuchsia::guest::EnvironmentControllerPtr environment_controller_;
  fuchsia::guest::InstanceControllerPtr instance_controller_;
  TestSerial serial_;
};

class ZirconGuestTest : public GuestTest {
 protected:
  fuchsia::guest::LaunchInfo LaunchInfo() override {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");
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

class ZirconGuestRamdiskTest : public GuestTest {
 protected:
  fuchsia::guest::LaunchInfo LaunchInfo() override {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");

    create_ramdisk(kVirtioBlockSize, kVirtioBlockCount, ramdisk_path_);
    launch_info.args.push_back(fxl::StringPrintf("--block=%s", ramdisk_path_));

    return launch_info;
  }

  zx_status_t WaitForPkgfs() {
    for (size_t i = 0; i != 20; ++i) {
      std::string ps;
      zx_status_t status = Execute("ps", &ps);
      if (status != ZX_OK) {
        return status;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(200)));
      auto pkgfs = ps.find("pkgfs");
      if (pkgfs != std::string::npos) {
        return ZX_OK;
      }
    }
    return ZX_ERR_BAD_STATE;
  }

  void SetUp() override {
    StartGuest(1);
    ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  }

  void TearDown() override { QuitLoop(); }

  char ramdisk_path_[PATH_MAX];
};

TEST_F(ZirconGuestRamdiskTest, BlockDeviceExists) {
  std::string cmd = fxl::StringPrintf("run %s#%s check %u %u", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kVirtioBlockSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconGuestRamdiskTest, Read) {
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kVirtioBlockSize];
  for (off_t offset = 0; offset != kVirtioBlockCount; ++offset) {
    memset(data, offset, kVirtioBlockSize);
    ASSERT_EQ(
        pwrite(fd.get(), &data, kVirtioBlockSize, offset * kVirtioBlockSize),
        kVirtioBlockSize);
    std::string cmd = fxl::StringPrintf(
        "run %s#%s read %u %u %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kVirtioBlockSize, kVirtioBlockCount, static_cast<int>(offset),
        static_cast<int>(offset));
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconGuestRamdiskTest, Write) {
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kVirtioBlockSize];
  for (off_t offset = 0; offset != kVirtioBlockCount; ++offset) {
    std::string cmd = fxl::StringPrintf(
        "run %s#%s write %u %u %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kVirtioBlockSize, kVirtioBlockCount, static_cast<int>(offset),
        static_cast<int>(offset));
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
    ASSERT_EQ(
        pread(fd.get(), &data, kVirtioBlockSize, offset * kVirtioBlockSize),
        kVirtioBlockSize);
    for (off_t i = 0; i != kVirtioBlockSize; ++i) {
      EXPECT_EQ(data[i], offset);
    }
  }
}

class LinuxGuestTest : public GuestTest {
 protected:
  fuchsia::guest::LaunchInfo LaunchInfo() override {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kLinuxGuestUrl;
    launch_info.args.push_back("--display=none");
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
