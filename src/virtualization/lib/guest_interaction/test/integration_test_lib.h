// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_

#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <src/virtualization/tests/fake_netstack.h>
#include <src/virtualization/tests/guest_console.h>

#include "src/lib/component/cpp/environment_services_helper.h"
#include "src/lib/component/cpp/testing/test_util.h"

static constexpr char kGuestLabel[] = "debian_guest";
static constexpr char kGuestManagerUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_manager#meta/guest_manager.cmx";
static constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";
static constexpr char kGuestDiscoveryUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_discovery_service#meta/guest_discovery_service.cmx";

// The host will copy kTestScriptSource to kGuestScriptDestination on the
// guest.  The host will then ask the guest to exec kGuestScriptDestination
// and feed kTestScriptInput to the guest process's stdin.  The script will
// will echo kTestStdout to stdout, kTestStderr to stderr, and kTestScriptInput
// to  kGuestFileOutputLocation.  The host will
// download the file to kHostOuputCopyLocation.
static constexpr char kTestScriptSource[] = "/pkg/data/test_script.sh";
static constexpr char kGuestScriptDestination[] = "/root/input/test_script.sh";
static constexpr char kTestStdout[] = "stdout";
static constexpr char kTestStderr[] = "stderr";
static constexpr char kTestScriptInput[] = "hello world\n";
static constexpr char kGuestFileOutputLocation[] = "/root/output/script_output.txt";
static constexpr char kHostOuputCopyLocation[] = "/data/copy";

class GuestInteractionTest : public sys::testing::TestWithEnvironment {
 public:
  void CreateEnvironment() {
    ASSERT_TRUE(services_ && !env_);

    env_ = CreateNewEnclosingEnvironment("GuestInteractionEnvironment", std::move(services_));
  }

  void LaunchDebianGuest() {
    // Launch the Debian guest
    fuchsia::virtualization::LaunchInfo guest_launch_info;
    guest_launch_info.url = kDebianGuestUrl;
    guest_launch_info.label = kGuestLabel;
    guest_launch_info.args.emplace({"--virtio-gpu=false"});

    fuchsia::virtualization::ManagerPtr guest_environment_manager;
    fuchsia::virtualization::GuestPtr guest_instance_controller;
    cid_ = -1;

    env_->ConnectToService(guest_environment_manager.NewRequest());
    guest_environment_manager->Create(fuchsia::netemul::guest::DEFAULT_REALM, realm_.NewRequest());
    realm_->LaunchInstance(std::move(guest_launch_info), guest_instance_controller.NewRequest(),
                           [&](uint32_t callback_cid) { cid_ = callback_cid; });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return cid_ >= 0; }, zx::sec(5)));

    // Start a GuestConsole.  When the console starts, it waits until it
    // receives some sensible output from the guest to ensure that the guest is
    // usable.
    zx::socket socket;
    guest_instance_controller->GetSerial([&socket](zx::socket s) { socket = std::move(s); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&socket] { return socket.is_valid(); }, zx::sec(30)));

    GuestConsole serial(std::make_unique<ZxSocket>(std::move(socket)));
    zx_status_t status = serial.Start();
    ASSERT_EQ(status, ZX_OK);

    // Wait until sysctl shows that the guest_interaction_daemon is running.
    RunLoopUntil([&status, &serial]() -> bool {
      std::string output;
      status = serial.ExecuteBlocking("systemctl is-active guest_interaction_daemon", "$", &output);

      // If the command cannot be executed, break out of the loop so the test can fail.
      if (status != ZX_OK) {
        return true;
      }

      // Ensure that the output from the command indicates that guest_interaction_daemon is
      // active.
      if (output.find("inactive") != std::string::npos) {
        return false;
      }
      return true;
    });

    ASSERT_EQ(status, ZX_OK);
  }

  uint32_t cid_;
  fuchsia::virtualization::RealmPtr realm_;
  std::unique_ptr<sys::testing::EnvironmentServices> services_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  FakeNetstack fake_netstack_;

 protected:
  void SetUp() {
    services_ = CreateServices();

    // Add Netstack services
    services_->AddService(fake_netstack_.GetHandler(), fuchsia::netstack::Netstack::Name_);
    services_->AddService(fake_netstack_.GetHandler(), fuchsia::net::stack::Stack::Name_);

    // Add guest service
    fuchsia::sys::LaunchInfo guest_manager_launch_info;
    guest_manager_launch_info.url = kGuestManagerUrl;
    guest_manager_launch_info.out = sys::CloneFileDescriptor(1);
    guest_manager_launch_info.err = sys::CloneFileDescriptor(2);
    services_->AddServiceWithLaunchInfo(std::move(guest_manager_launch_info),
                                        fuchsia::virtualization::Manager::Name_);
  }
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
