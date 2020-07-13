// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/inspect/test/llcpp/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/inspect/cpp/reader.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace {
using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::Controller;
using llcpp::fuchsia::device::inspect::test::TestInspect;

class InspectTestHelper {
 public:
  InspectTestHelper() {}

  void ReadInspect(const zx::vmo& vmo) {
    hierarchy_ = inspect::ReadFromVmo(vmo);
    ASSERT_TRUE(hierarchy_.is_ok());
  }

  inspect::Hierarchy& hierarchy() { return hierarchy_.value(); }

  template <typename T>
  void CheckProperty(const inspect::NodeValue& node, std::string property, T expected_value) {
    const T* actual_value = node.get_property<T>(property);
    ASSERT_TRUE(actual_value);
    EXPECT_EQ(expected_value.value(), actual_value->value());
  }

 private:
  fit::result<inspect::Hierarchy> hierarchy_;
};

class InspectTestCase : public InspectTestHelper, public zxtest::Test {
 public:
  ~InspectTestCase() override = default;

  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.path_prefix = "/pkg/";

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_INSPECT_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;

    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:13:0/inspect-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);
  }

  const IsolatedDevmgr& devmgr() { return devmgr_; }
  zx::unowned_channel channel() { return zx::unowned(chan_); }

 private:
  IsolatedDevmgr devmgr_;
  zx::channel chan_;
};

TEST_F(InspectTestCase, InspectDevfs) {
  fbl::unique_fd fd;
  // Check if inspect-test device is hosted in diagnostics folder
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFileReadOnly(devmgr().devfs_root(),
                                                                  "diagnostics/class", &fd));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFileReadOnly(
      devmgr().devfs_root(), "diagnostics/class/test/000.inspect", &fd));
  ASSERT_GT(fd.get(), 0);
}

TEST_F(InspectTestCase, ReadInspectData) {
  fbl::unique_fd fd;
  // Wait for inspect data to appear
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFileReadOnly(
      devmgr().devfs_root(), "diagnostics/class/test/000.inspect", &fd));
  ASSERT_GT(fd.get(), 0);
  zx_handle_t out_vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(fdio_get_vmo_clone(fd.get(), &out_vmo));
  ASSERT_NE(out_vmo, ZX_HANDLE_INVALID);

  // Check initial inspect data
  zx::vmo inspect_vmo(out_vmo);
  ASSERT_NO_FATAL_FAILURES(ReadInspect(inspect_vmo));
  // testBeforeDdkAdd: "OK"
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      hierarchy().node(), "testBeforeDdkAdd", inspect::StringPropertyValue("OK")));

  // Call test-driver to modify inspect data
  auto result = TestInspect::Call::ModifyInspect(channel());
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());

  // Verify new inspect data is reflected
  out_vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(fdio_get_vmo_clone(fd.get(), &out_vmo));
  ASSERT_NE(out_vmo, ZX_HANDLE_INVALID);
  inspect_vmo = zx::vmo(out_vmo);
  ASSERT_NO_FATAL_FAILURES(ReadInspect(inspect_vmo));
  // Previous values
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      hierarchy().node(), "testBeforeDdkAdd", inspect::StringPropertyValue("OK")));
  // New addition - testModify: "OK"
  ASSERT_NO_FATAL_FAILURES(CheckProperty<inspect::StringPropertyValue>(
      hierarchy().node(), "testModify", inspect::StringPropertyValue("OK")));
}

}  // namespace
