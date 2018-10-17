// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <thread>

#include "garnet/bin/network_time/timezone.h"
#include "garnet/public/lib/fxl/files/scoped_temp_dir.h"
#include "gmock/gmock.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fdio/util.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/substitute.h"
#include "local_roughtime_server.h"
#include "third_party/roughtime/protocol.h"

namespace time_server {

namespace chrono = std::chrono;

using chrono::steady_clock;
using chrono::system_clock;
using chrono::time_point;
using component::testing::CloneFileDescriptor;
using component::testing::EnclosingEnvironment;
using component::testing::TestWithEnvironment;
using files::ScopedTempDir;
using fuchsia::sys::LaunchInfo;
using fxl::StringPrintf;
using time_server::LocalRoughtimeServer;
using time_server::Timezone;

#define GARNET_BIN_NETWORK_TIME_TEST_PUBLIC_KEY                               \
  0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0,     \
      0x2a, 0x6f, 0x0d, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, \
      0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda, 0x29

// Ed25519 private key used by |simple_server|. The
// private part consists of all zeros and so is only for use in this example.
constexpr uint8_t kPrivateKey[roughtime::kPrivateKeyLength] = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, GARNET_BIN_NETWORK_TIME_TEST_PUBLIC_KEY};

// Same as the second half of the private key, but there's no sane way to avoid
// duplicating this code without macros.
constexpr uint8_t kPublicKey[roughtime::kPublicKeyLength] = {
    GARNET_BIN_NETWORK_TIME_TEST_PUBLIC_KEY};

#undef GARNET_BIN_NETWORK_TIME_TEST_PUBLIC_KEY

// 0-indexed month
constexpr uint8_t kOctober = 9;
constexpr char kNetworkTimePackage[] =
    "fuchsia-pkg://fuchsia.com/network_time#meta/network_time.cmx";

// Copied from zircon/lib/fidl/array_to_string
std::string to_hex_string(const uint8_t* data, size_t size) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(size * 2);
  for (size_t i = 0; i < size; i++) {
    unsigned char c = data[i];
    ret.push_back(kHexadecimalCharacters[c >> 4]);
    ret.push_back(kHexadecimalCharacters[c & 0xf]);
  }
  return ret;
}

// Integration tests for |Timezone|.
class SystemTimeUpdaterTest : public TestWithEnvironment {
 protected:
  static constexpr uint16_t kPortNumber = 19707;

  void SetUp() override {
    TestWithEnvironment::SetUp();
    monotonic_start_time_ = steady_clock::now();
    utc_start_time_ = system_clock::now();
  }

  void TearDown() override {
    ResetClock();
    TestWithEnvironment::TearDown();
  }

  // Launch a local Roughtime server in a new thread.
  std::unique_ptr<std::thread> LaunchLocalRoughtimeServer(
      uint16_t port_number) {
    local_roughtime_server_ = LocalRoughtimeServer::MakeInstance(
        kPrivateKey, port_number, 1537485257118'000);
    return std::make_unique<std::thread>(
        std::thread([&]() { local_roughtime_server_->Start(); }));
  }

  // Reset the system clock to the correct time, captured before the test and
  // adjusted for elapsed time.
  void ResetClock() {
    const auto elapsed_monotonic_duration =
        steady_clock::now() - monotonic_start_time_;
    const auto expected_utc_time = utc_start_time_ + elapsed_monotonic_duration;
    const time_t expected_epoch_seconds =
        chrono::duration_cast<chrono::seconds>(
            expected_utc_time.time_since_epoch())
            .count();
    Timezone::SetSystemTime(expected_epoch_seconds);
  }

  // Launch the system time update service using the production config file.
  fuchsia::sys::ComponentControllerPtr
  LaunchSystemTimeUpdateServiceWithDefaultServers() {
    return LaunchSystemTimeUpdateService(nullptr);
  }

  fuchsia::sys::ComponentControllerPtr
  LaunchSystemTimeUpdateServiceForLocalServer(uint16_t port_number) {
    // Note that the host must explicitly be "::1". "localhost" is
    // misinterpreted as implying IPv4.
    const std::string kClientConfigJson = StringPrintf(
        R"(
{
  "servers":
  [
    {
      "name": "Local",
      "publicKey": "%s",
      "addresses":
        [
          {
            "address": "::1:%d"
          }
        ]
    }
  ]
})",
        to_hex_string(kPublicKey, roughtime::kPublicKeyLength).c_str(),
        port_number);
    std::string client_config_path;
    temp_dir_.NewTempFileWithData(kClientConfigJson, &client_config_path);
    return LaunchSystemTimeUpdateService(client_config_path.c_str());
  }

  // Run a loop until the given component is terminated or |timeout| elapses.
  void RunUntilTerminatedOrTimeout(
      fuchsia::sys::ComponentControllerPtr component_controller,
      const zx::duration timeout) {
    bool is_terminated = false;
    component_controller.events().OnTerminated =
        [&](int64_t return_code, fuchsia::sys::TerminationReason reason) {
          is_terminated = true;
        };
    RunLoopWithTimeoutOrUntil([&]() { return is_terminated; }, timeout,
                              zx::sec(1));
  }

  std::unique_ptr<LocalRoughtimeServer> local_roughtime_server_ = nullptr;

 private:
  // Launch the system time update service, using the given config path. If
  // |opt_pathname| is null, then the production config file will be used.
  fuchsia::sys::ComponentControllerPtr LaunchSystemTimeUpdateService(
      const char* opt_pathname) {
    LaunchInfo launch_info;
    launch_info.url = kNetworkTimePackage;
    launch_info.out = CloneFileDescriptor(STDOUT_FILENO);
    launch_info.err = CloneFileDescriptor(STDERR_FILENO);

    fxl::UniqueFD tmp_dir_fd(open("/tmp", O_DIRECTORY | O_RDONLY));
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    launch_info.flat_namespace->paths.push_back("/tmp");
    launch_info.flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(tmp_dir_fd.get()));

    if (opt_pathname != nullptr) {
      launch_info.arguments.push_back(
          StringPrintf("--config=%s", opt_pathname));
    }

    fuchsia::sys::ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info),
                                        controller.NewRequest());
    return controller;
  }
  ScopedTempDir temp_dir_;
  time_point<system_clock> utc_start_time_;
  time_point<steady_clock> monotonic_start_time_;
};

// Match the GMT date of the given |time_point<system_clock>|. Time differences
// smaller than one day are ignored.
// Args:
//   expected_year: uint16_t
//   expected_month: uint8_t, 0-11
//   expected_day: uint8_t, 1-31
MATCHER_P3(EqualsGmtDate, expected_year, expected_month, expected_day,
           "has GMT date {" + testing::PrintToString(expected_year) + ", " +
               testing::PrintToString(expected_month + 1) + ", " +
               testing::PrintToString(expected_day) + "}") {
  time_t actual = system_clock::to_time_t(arg);
  tm* tm = std::gmtime(&actual);
  if (tm->tm_year + 1900 == expected_year && tm->tm_mon == expected_month &&
      tm->tm_mday == expected_day) {
    return true;
  }
  *result_listener << "GMT date {" << tm->tm_year + 1900 << ", "
                   << tm->tm_mon + 1 << ", " << tm->tm_mday << "}";
  return false;
};

TEST_F(SystemTimeUpdaterTest, UpdateTimeFromLocalRoughtimeServer) {
  // Launch the roughtime server in a separate thread.
  const std::unique_ptr<std::thread> server_thread =
      LaunchLocalRoughtimeServer(kPortNumber);
  // We detach the server thread instead of joining it because
  // |SimpleServer::ProcessBatch| might run indefinitely. There is no clean way
  // to terminate the server thread.
  server_thread->detach();

  uint16_t port_number = local_roughtime_server_->GetPortNumber();
  ASSERT_GT(port_number, 0);

  RunLoopWithTimeoutOrUntil(
      [&]() { return local_roughtime_server_->IsRunning(); }, zx::sec(10),
      zx::sec(1));
  ASSERT_TRUE(local_roughtime_server_->IsRunning());

  // Would use 1985-10-26, but it's considered too far in the past.
  local_roughtime_server_->SetTime(2000, kOctober, 26, 9, 0, 0);
  RunUntilTerminatedOrTimeout(
      LaunchSystemTimeUpdateServiceForLocalServer(port_number), zx::sec(20));
  EXPECT_THAT(system_clock::now(), EqualsGmtDate(2000, kOctober, 26));

  // Back to the future...
  local_roughtime_server_->SetTime(2015, kOctober, 21, 7, 28, 0);
  RunUntilTerminatedOrTimeout(
      LaunchSystemTimeUpdateServiceForLocalServer(port_number), zx::sec(20));
  EXPECT_THAT(system_clock::now(), EqualsGmtDate(2015, kOctober, 21));

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
  component_controller.events().OnTerminated =
      [&](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        EXPECT_EQ(reason, fuchsia::sys::TerminationReason::EXITED);
        EXPECT_EQ(return_code, EXIT_SUCCESS);
        is_terminated = true;
      };
  RunLoopWithTimeoutOrUntil([&]() { return is_terminated; }, timeout,
                            zx::sec(1));
  EXPECT_TRUE(is_terminated);
}

}  // namespace time_server