// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_tests.h"

#include <fuchsia/device/test/c/fidl.h>
#include <getopt.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <array>

#include <ddk/platform-defs.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

uint32_t test_log_level = 0;
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
  // /boot/ is for bringup builds, /system/ is for core/workstation/etc.
  args.driver_search_paths.push_back("/pkg/bin");
  args.driver_search_paths.push_back("/boot/driver");
  args.driver_search_paths.push_back("/system/driver");
  args.device_list.push_back(kDeviceEntry);
  args.disable_block_watcher = true;
  args.disable_netsvc = true;

  switch (test_log_level) {
    case 1:
      args.boot_args["driver.fake_pci_bus_driver.log"] = "debug";
      break;
    case 2:
      args.boot_args["driver.fake_pci_bus_driver.log"] = "trace";
      break;
  }
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

int main(int argc, char* argv[]) {
  int opt;
  int v_position = 0;
  while (!v_position && (opt = getopt(argc, argv, "hv")) != -1) {
    switch (opt) {
      case 'v':
        test_log_level++;
        v_position = optind - 1;
        break;
      case 'h':
        fprintf(stderr,
                "    Test-Specific Usage: %s [OPTIONS]\n\n"
                "    [OPTIONS]\n"
                "    -v                                                  Enable DEBUG logs\n"
                "    -vv                                                 Enable TRACE logs\n\n",
                argv[0]);
        break;
    }
  }

  // Do the minimal amount of work to forward the rest of the args to zxtest
  // with our consumed '-v' / '-vv' removed. Don't worry about additional -v
  // usage because the zxtest help will point out the invalid nature of it.
  if (v_position) {
    for (int p = v_position; p < argc - 1; p++) {
      argv[p] = argv[p + 1];
    }
    argc--;
  }
  return RUN_ALL_TESTS(argc, argv);
}
