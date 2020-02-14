// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_tests.h"

#include <fuchsia/device/test/c/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <array>

#include <ddk/platform-defs.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

const board_test::DeviceEntry kDeviceEntry = []() {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, kFakeBusDriverName);
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_PCI_TEST;
  entry.did = 0;
  return entry;
}();

class PciDriverTests : public zxtest::Test {
 protected:
  IsolatedDevmgr devmgr_;
  fbl::unique_fd pcictl_fd_;
  fbl::unique_fd protocol_fd_;
};

// This test builds the foundation for PCI Protocol tests. After the
// IsolatedDevmgr loads a new platform bus, it will bind the fake PCI bus
// driver. The fake bus driver creates a real device backed by the fake ecam,
// which results in our protocol test driver being loaded. The protocol test
// driver exposes a FIDL RunTests interface for the test runner to request tests
// be run and receive a summary report. Protocol tests are run in the proxied
// devhost against the real PCI protcol implementation speaking to a real PCI
// device interface, backed by the fake bus driver.
//
// Illustrated:
//
// TestRunner(driver_tests) -> pbus -> fake_pci <-> ProtocolTestDriver(pci.proxy)
//       \---------------> Fuchsia.Device.Test <-------------/
TEST_F(PciDriverTests, TestRunner) {
  IsolatedDevmgr::Args args;
  // /boot/driver is used for finding and loading a platform bus driver, while
  // /boot/driver/test is where pcictl's .so will be due it being build via the
  // test_driver() rule.
  args.driver_search_paths.push_back("/boot/driver");
  args.device_list.push_back(kDeviceEntry);
  args.disable_block_watcher = true;
  args.disable_netsvc = true;
  zx_status_t st = IsolatedDevmgr::Create(&args, &devmgr_);
  ASSERT_OK(st);

  // The final path is made up of the FakeBusDriver, the bind point it creates, and
  // the final protocol test driver.
  std::array<char, 64> proto_driver_path = {};
  snprintf(proto_driver_path.data(), proto_driver_path.max_size(),
           "sys/platform/%02x:%02x:%01x/%s/%02x:%02x.%1x/%s", kDeviceEntry.vid, kDeviceEntry.pid,
           kDeviceEntry.did, kDeviceEntry.name, PCI_TEST_BUS_ID, PCI_TEST_DEV_ID, PCI_TEST_FUNC_ID,
           kProtocolTestDriverName);
  st = devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), proto_driver_path.data(),
                                                     &protocol_fd_);
  ASSERT_OK(st);
  zx::channel ch;
  struct fuchsia_device_test_TestReport report = {};

  ASSERT_OK(fdio_get_service_handle(protocol_fd_.release(), ch.reset_and_get_address()));
  // Flush the ouput to this point so it doesn't interleave with the proxy's
  // test output.
  fflush(stdout);
  ASSERT_OK(fuchsia_device_test_TestRunTests(ch.get(), &st, &report));
  ASSERT_OK(st);
  ASSERT_NE(report.test_count, 0);
  ASSERT_EQ(report.test_count, report.success_count);
  EXPECT_EQ(report.failure_count, 0);
}
