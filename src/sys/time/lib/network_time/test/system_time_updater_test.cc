// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <time.h>

#include <thread>

#include <gmock/gmock.h>

#include "common.h"
#include "fake_rtc_device.h"
#include "fuchsia/hardware/rtc/cpp/fidl.h"
#include "local_roughtime_server.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "third_party/roughtime/protocol.h"

namespace time_server {

namespace chrono = std::chrono;
namespace rtc = fuchsia::hardware::rtc;

using chrono::steady_clock;
using chrono::system_clock;
using chrono::time_point;
using files::ScopedTempDir;
using fuchsia::sys::LaunchInfo;
using fxl::StringPrintf;
using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;
using sys::testing::TestWithEnvironment;
using time_server::FakeRtcDevice;
using time_server::LocalRoughtimeServer;

constexpr char kNetworkTimePackage[] =
    "fuchsia-pkg://fuchsia.com/network-time-service#meta/network_time_service.cmx";

constexpr char kFakeDevPath[] = "/fakedev";
constexpr char kRtcServiceName[] = "fuchsia.hardware.rtc.Device";
// C++ still doesn't support compile-time string concatenation. Keep this in
// sync with kFakeDevPath and kRtcServiceName.
constexpr char kFakeRtcDevicePath[] = "/fakedev/fuchsia.hardware.rtc.Device";

// Integration tests for |Timezone|.
class SystemTimeUpdaterTest : public TestWithEnvironment {
 protected:
  static constexpr uint16_t kPortNumber = 19707;

  void SetUp() override {
    TestWithEnvironment::SetUp();

    // Make a fake RTC device and a PseudoDir, and serve the RTC device at that
    // PseudoDir.
    fake_dev_vfs_dir_ = std::make_unique<vfs::PseudoDir>();
    fake_rtc_device_ = std::make_unique<FakeRtcDevice>();
    std::unique_ptr<vfs::Service> fake_rtc_service =
        std::make_unique<vfs::Service>(fake_rtc_device_->GetHandler());
    ASSERT_EQ(ZX_OK, fake_dev_vfs_dir_->AddEntry(kRtcServiceName, std::move(fake_rtc_service)));
  }

  void TearDown() override { TestWithEnvironment::TearDown(); }

  // Launch a local Roughtime server in a new thread.  The new thread owns the
  // local Roughtime server because it is going to outlive the test.
  std::unique_ptr<std::thread> LaunchLocalRoughtimeServer(uint16_t port_number) {
    auto local_roughtime_server =
        LocalRoughtimeServer::MakeInstance(kTestPrivateKey, port_number, 1537485257118'000);
    local_roughtime_server_ = local_roughtime_server.release();
    return std::make_unique<std::thread>(std::thread([&]() {
      local_roughtime_server_->Start();
      delete local_roughtime_server_;
    }));
  }

  // Launch the system time update service using the production config file.
  fuchsia::sys::ComponentControllerPtr LaunchSystemTimeUpdateServiceWithDefaultServers() {
    return LaunchSystemTimeUpdateService(nullptr);
  }

  fuchsia::sys::ComponentControllerPtr LaunchSystemTimeUpdateServiceForLocalServer(
      uint16_t port_number) {
    std::string config_json = local_client_config(port_number);
    std::string client_config_path;
    temp_dir_.NewTempFileWithData(config_json, &client_config_path);
    return LaunchSystemTimeUpdateService(client_config_path.c_str());
  }

  std::unique_ptr<vfs::PseudoDir> fake_dev_vfs_dir_ = nullptr;
  std::unique_ptr<FakeRtcDevice> fake_rtc_device_ = nullptr;
  LocalRoughtimeServer* local_roughtime_server_ = nullptr;

 private:
  // Launch the system time update service, using the given config path. If
  // |opt_pathname| is null, then the production config file will be used.
  fuchsia::sys::ComponentControllerPtr LaunchSystemTimeUpdateService(const char* opt_pathname) {
    zx_status_t status;

    LaunchInfo launch_info;
    launch_info.url = kNetworkTimePackage;
    launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
    launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);

    fbl::unique_fd tmp_dir_fd(open("/tmp", O_DIRECTORY | O_RDONLY));
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/tmp");
    launch_info.flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(tmp_dir_fd.get()));

    launch_info.arguments.emplace();
    if (opt_pathname != nullptr) {
      launch_info.arguments->push_back(StringPrintf("--config=%s", opt_pathname));
    }

    // Specify the service path at which to find a fake RTC device.
    launch_info.arguments->push_back(StringPrintf("--rtc_path=%s", kFakeRtcDevicePath));
    launch_info.arguments->push_back("--immediate");

    // fuchsia::io::Directory is the directory interface that we expose to the
    // OS. vfs::PseudoDir is the C++ object that implements the
    // fuchsia::io::Directory in our process. Here, we bind the interface to the
    // implementation.
    fidl::InterfaceHandle<fuchsia::io::Directory> fake_dev_io_dir;
    status = fake_dev_vfs_dir_->Serve(
        fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
        fake_dev_io_dir.NewRequest().TakeChannel(), dispatcher());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Couldn't Serve() fake dev dir";
    }

    // Note that the indices of `paths` and `directories` have to line up.
    launch_info.flat_namespace->paths.push_back(kFakeDevPath);
    launch_info.flat_namespace->directories.push_back(fake_dev_io_dir.TakeChannel());

    fuchsia::sys::ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info), controller.NewRequest());
    return controller;
  }

  ScopedTempDir temp_dir_;
};

// Match the GMT date of the given |rtc::Time|. Time differences
// smaller than one day are ignored.
// Args:
//   expected_year: uint16_t
//   expected_month: uint8_t, 1-12
//   expected_day: uint8_t, 1-31
MATCHER_P3(EqualsGmtDate, expected_year, expected_month, expected_day,
           "has GMT date {" + testing::PrintToString(expected_year) + ", " +
               testing::PrintToString(expected_month) + ", " +
               testing::PrintToString(expected_day) + "}") {
  const rtc::Time actual = arg;
  if (actual.year == expected_year && actual.month == expected_month &&
      actual.day == expected_day) {
    return true;
  }
  *result_listener << "GMT date {" << actual.year << ", " << unsigned(actual.month) << ", "
                   << unsigned(actual.day) << "}";
  return false;
};

TEST_F(SystemTimeUpdaterTest, UpdateTimeFromLocalRoughtimeServer) {
  // Launch the roughtime server in a separate thread.
  const std::unique_ptr<std::thread> server_thread = LaunchLocalRoughtimeServer(kPortNumber);
  // We detach the server thread instead of joining it because
  // |SimpleServer::ProcessBatch| might run indefinitely. There is no clean way
  // to terminate the server thread.
  server_thread->detach();

  uint16_t port_number = local_roughtime_server_->GetPortNumber();
  ASSERT_GT(port_number, 0);

  RunLoopWithTimeoutOrUntil([&]() { return local_roughtime_server_->IsRunning(); }, zx::sec(10),
                            zx::sec(1));
  ASSERT_TRUE(local_roughtime_server_->IsRunning());

  // Back to the past...
  local_roughtime_server_->SetTime(1985, 10, 26, 9, 0, 0);
  RunComponentUntilTerminated(LaunchSystemTimeUpdateServiceForLocalServer(port_number), nullptr);
  EXPECT_THAT(fake_rtc_device_->Get(), EqualsGmtDate(1985, 10, 26));

  // Back to the future...
  local_roughtime_server_->SetTime(2015, 10, 21, 7, 28, 0);
  RunComponentUntilTerminated(LaunchSystemTimeUpdateServiceForLocalServer(port_number), nullptr);
  EXPECT_THAT(fake_rtc_device_->Get(), EqualsGmtDate(2015, 10, 21));

  local_roughtime_server_->Stop();
  // Can't do anything to clean up the server thread.
}

// Requires internet access.
// TODO(CP-131): Split out into a separate test that can run on CI, not CQ.
TEST_F(SystemTimeUpdaterTest, DISABLED_UpdateTimeFromPublicRoughtimeServer) {
  fuchsia::sys::ComponentControllerPtr component_controller =
      LaunchSystemTimeUpdateServiceWithDefaultServers();
  const zx::duration timeout = zx::sec(20);
  bool is_terminated = false;
  component_controller.events().OnTerminated = [&](int64_t return_code,
                                                   fuchsia::sys::TerminationReason reason) {
    EXPECT_EQ(reason, fuchsia::sys::TerminationReason::EXITED);
    EXPECT_EQ(return_code, EXIT_SUCCESS);
    is_terminated = true;
  };
  RunLoopWithTimeoutOrUntil([&]() { return is_terminated; }, timeout, zx::sec(1));
  EXPECT_TRUE(is_terminated);
}

}  // namespace time_server
