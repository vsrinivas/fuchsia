// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
#define SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "src/virtualization/tests/fake_input_only_scenic.h"
#include "src/virtualization/tests/guest_console.h"
#include "src/virtualization/tests/mock_netstack.h"

static constexpr char kZirconGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cmx";
static constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";

enum class GuestKernel {
  ZIRCON,
  LINUX,
};

// EnclosedGuest is a base class that defines an guest environment and instance
// encapsulated in an EnclosingEnvironment. A derived class must define the
// |LaunchInfo| to send to the guest environment controller, as well as methods
// for waiting for the guest to be ready and running test utilities. Most tests
// will derive from either ZirconEnclosedGuest or DebianEnclosedGuest below and
// override LaunchInfo only. EnclosedGuest is designed to be used with
// GuestTest.
class EnclosedGuest {
 public:
  EnclosedGuest()
      : loop_(&kAsyncLoopConfigAttachToThread),
        real_services_(sys::ServiceDirectory::CreateFromNamespace()) {}
  virtual ~EnclosedGuest() {}

  zx_status_t Start();
  void Stop() { loop_.Quit(); }

  bool Ready() const { return ready_; }

  // Execute |command| on the guest serial and wait for the |result|.
  virtual zx_status_t Execute(const std::vector<std::string>& argv,
                              std::string* result = nullptr);

  // Run a test util named |util| with |argv| in the guest and wait for the
  // |result|.
  zx_status_t RunUtil(const std::string& util,
                      const std::vector<std::string>& argv,
                      std::string* result = nullptr);

  // Return a shell command for a test utility named |util| with the given
  // |argv| in the guest. The result may be passed directly to |Execute|
  // to actually run the command.
  virtual std::vector<std::string> GetTestUtilCommand(
      const std::string& util, const std::vector<std::string>& argv) = 0;

  virtual GuestKernel GetGuestKernel() = 0;

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint>
          endpoint) {
    realm_->GetHostVsockEndpoint(std::move(endpoint));
  }

  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController>
          balloon_controller) {
    realm_->ConnectToBalloon(guest_cid_, std::move(balloon_controller));
  }

  uint32_t GetGuestCid() const { return guest_cid_; }

  MockNetstack* GetNetstack() { return &mock_netstack_; }

  FakeInputOnlyScenic* GetScenic() { return &fake_scenic_; }

  GuestConsole* GetConsole() { return &console_; }

 protected:
  // Provides guest specific |launch_info|, called by Start.
  virtual zx_status_t LaunchInfo(
      fuchsia::virtualization::LaunchInfo* launch_info) = 0;

  // Waits until the guest is ready to run test utilities, called by Start.
  virtual zx_status_t WaitForSystemReady() = 0;

  virtual std::string ShellPrompt() = 0;

 private:
  async::Loop loop_;
  std::shared_ptr<sys::ServiceDirectory> real_services_;
  fuchsia::sys::EnvironmentPtr real_env_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_environment_;
  fuchsia::virtualization::ManagerPtr manager_;
  fuchsia::virtualization::RealmPtr realm_;
  fuchsia::virtualization::GuestPtr guest_;
  FakeInputOnlyScenic fake_scenic_;
  MockNetstack mock_netstack_;
  GuestConsole console_;
  uint32_t guest_cid_;
  bool ready_ = false;
};

class ZirconEnclosedGuest : public EnclosedGuest {
 public:
  std::vector<std::string> GetTestUtilCommand(
      const std::string& util, const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::ZIRCON; }

 protected:
  zx_status_t LaunchInfo(
      fuchsia::virtualization::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
  std::string ShellPrompt() override { return "$ "; }
};

class DebianEnclosedGuest : public EnclosedGuest {
 public:
  std::vector<std::string> GetTestUtilCommand(
      const std::string& util, const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

 protected:
  zx_status_t LaunchInfo(
      fuchsia::virtualization::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
  std::string ShellPrompt() override { return "$ "; }
};

#endif  // SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
