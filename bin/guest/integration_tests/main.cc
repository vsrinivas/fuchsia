// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <string>

#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <gmock/gmock.h>
#include <lib/fdio/util.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/socket.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/bin/guest/integration_tests/enclosed_guest.h"

using ::testing::HasSubstr;

static constexpr char kZirconGuestUrl[] = "zircon_guest";
static constexpr char kLinuxGuestUrl[] = "linux_guest";
static constexpr char kTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";
static constexpr char kVirtioBlockUtilCmx[] = "meta/virtio_block_test_util.cmx";

// TODO(alexlegg): Use the block size from the virtio-block implementation
// directly.
static constexpr uint32_t kVirtioBlockSize = 512;
static constexpr uint32_t kVirtioBlockCount = 32;

template <class T>
class GuestTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    enclosed_guest_ = new EnclosedGuest();
    ASSERT_EQ(enclosed_guest_->Start(T::LaunchInfo()), ZX_OK);
    ASSERT_TRUE(T::SetUpGuest());
    setup_succeeded_ = true;
  }

  static bool SetUpGuest() { return true; }

  static void TearDownTestCase() {
    enclosed_guest_->Stop();
    delete enclosed_guest_;
  }

  static zx_status_t WaitForPkgfs() {
    for (size_t i = 0; i != 20; ++i) {
      std::string ps;
      zx_status_t status = Execute("ps", &ps);
      if (status != ZX_OK) {
        return status;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(200)));
      auto pkgfs = ps.find("pkgfs");
      if (pkgfs != std::string::npos) {
        return ZX_OK;
      }
    }
    return ZX_ERR_BAD_STATE;
  }

  void SetUp() {
    // An assertion failure in SetUpTestCase doesn't prevent tests from running,
    // so we need to check that it succeeded here.
    ASSERT_TRUE(setup_succeeded_) << "Guest setup failed";
  }

  static zx_status_t Execute(std::string message,
                             std::string* result = nullptr) {
    return enclosed_guest_->Execute(message, result);
  }

 private:
  static bool setup_succeeded_;
  static EnclosedGuest* enclosed_guest_;
};

template <class T>
bool GuestTest<T>::setup_succeeded_ = false;
template <class T>
EnclosedGuest* GuestTest<T>::enclosed_guest_ = nullptr;

class ZirconGuestTest : public GuestTest<ZirconGuestTest> {
 public:
  static fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--cpus=1");
    return launch_info;
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
  static fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--cpus=4");
    return launch_info;
  }
};

TEST_F(ZirconMultiprocessorGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(Execute("echo \"test\"", &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}

class ZirconRamdiskGuestTest : public GuestTest<ZirconRamdiskGuestTest> {
 public:
  static fuchsia::guest::LaunchInfo LaunchInfo() {
    create_ramdisk(kVirtioBlockSize, kVirtioBlockCount, ramdisk_path_);
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kZirconGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--cpus=1");
    launch_info.args.push_back(fxl::StringPrintf("--block=%s", ramdisk_path_));
    return launch_info;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char ramdisk_path_[PATH_MAX];
};

char ZirconRamdiskGuestTest::ramdisk_path_[PATH_MAX] = "";

TEST_F(ZirconRamdiskGuestTest, BlockDeviceExists) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  std::string cmd = fxl::StringPrintf("run %s#%s check %u %u", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kVirtioBlockSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconRamdiskGuestTest, Read) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
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

TEST_F(ZirconRamdiskGuestTest, Write) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
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

class LinuxGuestTest : public GuestTest<LinuxGuestTest> {
 public:
  static fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kLinuxGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--cmdline-append=loglevel=0 console=hvc0");
    return launch_info;
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
  static fuchsia::guest::LaunchInfo LaunchInfo() {
    fuchsia::guest::LaunchInfo launch_info;
    launch_info.url = kLinuxGuestUrl;
    launch_info.args.push_back("--display=none");
    launch_info.args.push_back("--cmdline-append=loglevel=0 console=hvc0");
    return launch_info;
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
