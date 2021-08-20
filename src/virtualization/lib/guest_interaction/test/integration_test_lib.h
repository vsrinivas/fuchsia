// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_

#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>

#include <src/virtualization/tests/fake_netstack.h>
#include <src/virtualization/tests/guest_console.h>

#include "src/lib/testing/predicates/status.h"

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

class GuestInteractionTest : public gtest::TestWithEnvironmentFixture {
 public:
  void CreateEnvironment() {
    ASSERT_NE(services_, nullptr);
    ASSERT_EQ(env_, nullptr);

    env_ = CreateNewEnclosingEnvironment("GuestInteractionEnvironment", std::move(services_));
  }

  void LaunchDebianGuest() {
    // Launch the Debian guest
    fuchsia::virtualization::GuestConfig cfg;
    cfg.set_virtio_gpu(false);

    fuchsia::virtualization::ManagerPtr manager;
    fuchsia::virtualization::GuestPtr guest;

    env_->ConnectToService(manager.NewRequest());
    manager->Create(fuchsia::netemul::guest::DEFAULT_REALM, realm_.NewRequest());
    realm_->LaunchInstance(kDebianGuestUrl, kGuestLabel, std::move(cfg), guest.NewRequest(),
                           [this](uint32_t cid) { cid_ = cid; });
    RunLoopUntil([this]() { return cid_.has_value(); });

    // Start a GuestConsole.  When the console starts, it waits until it
    // receives some sensible output from the guest to ensure that the guest is
    // usable.
    std::optional<fuchsia::virtualization::Guest_GetConsole_Result> get_console_result;
    guest->GetConsole(
        [&get_console_result](fuchsia::virtualization::Guest_GetConsole_Result result) {
          get_console_result = std::move(result);
        });
    RunLoopUntil([&get_console_result]() { return get_console_result.has_value(); });
    fuchsia::virtualization::Guest_GetConsole_Result& result = get_console_result.value();
    switch (result.Which()) {
      case fuchsia::virtualization::Guest_GetConsole_Result::Tag::kResponse: {
        GuestConsole serial(std::make_unique<ZxSocket>(std::move(result.response().socket)));
        ASSERT_OK(serial.Start(zx::time::infinite()));

        // Wait until sysctl shows that the guest_interaction_daemon is running.
        ASSERT_OK(serial.ExecuteBlocking(
            "journalctl -f --no-tail -u guest_interaction_daemon | grep -m1 Listening", "$",
            zx::time::infinite(), nullptr));
        break;
      }
      case fuchsia::virtualization::Guest_GetConsole_Result::Tag::kErr:
        FAIL() << zx_status_get_string(result.err());
      case fuchsia::virtualization::Guest_GetConsole_Result::Tag::Invalid:
        FAIL() << "fuchsia.virtualization/Guest.GetConsole: invalid FIDL tag";
    }
  }

 protected:
  void SetUp() override {
    services_ = CreateServices();

    // Add Netstack services
    services_->AddService(fake_netstack_.GetHandler(), fuchsia::netstack::Netstack::Name_);
    services_->AddService(fake_netstack_.GetHandler(), fuchsia::net::stack::Stack::Name_);

    // Add guest service
    services_->AddServiceWithLaunchInfo(
        {
            .url = kGuestManagerUrl,
            .out = sys::CloneFileDescriptor(1),
            .err = sys::CloneFileDescriptor(2),
        },
        fuchsia::virtualization::Manager::Name_);

    // Allow hypervisor resource for virtualization.
    services_->AllowParentService(fuchsia::kernel::HypervisorResource::Name_);
    // Allow vmex resource for virtualization.
    services_->AllowParentService(fuchsia::kernel::VmexResource::Name_);
  }

  uint32_t cid() const { return cid_.value(); }
  const fuchsia::virtualization::RealmPtr& realm() const { return realm_; }
  sys::testing::EnvironmentServices& services() { return *services_; };
  sys::testing::EnclosingEnvironment& env() { return *env_; };

 private:
  std::optional<uint32_t> cid_;
  fuchsia::virtualization::RealmPtr realm_;
  std::unique_ptr<sys::testing::EnvironmentServices> services_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  FakeNetstack fake_netstack_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_TEST_INTEGRATION_TEST_LIB_H_
