// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_LIB_ENCLOSED_GUEST_H_
#define SRC_VIRTUALIZATION_TESTS_LIB_ENCLOSED_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fitx/result.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <memory>

#include <gtest/gtest.h>

#include "lib/async/dispatcher.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"
#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/lib/vsh/command_runner.h"
#include "src/virtualization/tests/lib/fake_netstack.h"
#include "src/virtualization/tests/lib/guest_console.h"
#include "src/virtualization/tests/lib/socket_logger.h"

enum class GuestKernel {
  ZIRCON,
  LINUX,
};

struct GuestLaunchInfo {
  std::string url;
  std::string interface_name;
  fuchsia::virtualization::GuestConfig config;
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
  explicit EnclosedGuest(async::Loop& loop) : loop_(loop) {}
  virtual ~EnclosedGuest() {}

  // Start the guest. `Start` is the preferred way to start the guest. If the realm
  // needs to be customized, `Start` can be replaced by a call to `InstallInRealm`
  // followed by a call to `LaunchInRealm`. This should follow a pattern like:
  //
  // ```
  // GuestLaunchInfo guest_launch_info;
  // realm_builder = RealmBuilder::Create();
  // InstallInRealm(realm_builder, guest_launch_info);
  // ...
  // ... // customize realm_builder
  // ...
  // RealmRoot realm_root(realm_builder.Build(dispatcher));
  // LaunchInRealm(realm_root, guest_launch_info, deadline);
  // ```
  //
  // Abort with ZX_ERR_TIMED_OUT if we reach `deadline` before the guest has started.
  zx_status_t Start(zx::time deadline);
  virtual void InstallInRealm(component_testing::Realm& realm, GuestLaunchInfo& guest_launch_info);
  zx_status_t LaunchInRealm(std::unique_ptr<sys::ServiceDirectory> services,
                            GuestLaunchInfo& guest_launch_info, zx::time deadline);

  // Provides guest specific launch info, called by Start.
  virtual zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) = 0;

  // Attempt to gracefully stop the guest.
  //
  // Abort with ZX_ERR_TIMED_OUT if we reach `deadline` first.
  zx_status_t Stop(zx::time deadline);

  // Execute |command| on the guest serial and wait for the |result|.
  virtual zx_status_t Execute(const std::vector<std::string>& argv,
                              const std::unordered_map<std::string, std::string>& env,
                              zx::time deadline, std::string* result = nullptr,
                              int32_t* return_code = nullptr);

  // Run a test util named |util| with |argv| in the guest and wait for the
  // |result|.
  zx_status_t RunUtil(const std::string& util, const std::vector<std::string>& argv,
                      zx::time deadline, std::string* result = nullptr);

  bool RunLoopUntil(fit::function<bool()> condition, zx::time deadline);

  // Return a shell command for a test utility named |util| with the given
  // |argv| in the guest. The result may be passed directly to |Execute|
  // to actually run the command.
  virtual std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                                      const std::vector<std::string>& argv) = 0;

  virtual GuestKernel GetGuestKernel() = 0;

  fitx::result<::fuchsia::virtualization::GuestError> ConnectToBalloon(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::BalloonController> controller);

  fitx::result<::fuchsia::virtualization::GuestError> GetHostVsockEndpoint(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint);

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(
      const std::string& interface_name = Interface::Name_) const {
    return realm_services_->Connect<Interface>(interface_name);
  }

  uint32_t GetGuestCid() const { return guest_cid_; }

  FakeNetstack* GetNetstack() { return &fake_netstack_; }

  std::optional<GuestConsole>& GetConsole() { return console_; }

  // Waits for a view to be created and presented using the GraphicalPresenter protocol.
  //
  // This is appropriate for tests that interact with UI stack in some way and need to interact with
  // the test UI stack.
  struct DisplayInfo {
    uint32_t width;
    uint32_t height;
  };
  DisplayInfo WaitForDisplay();

 protected:
  // Waits until the guest is ready to run test utilities, called by Start.
  virtual zx_status_t WaitForSystemReady(zx::time deadline) = 0;

  // Waits for the guest to perform a graceful shutdown.
  virtual zx_status_t ShutdownAndWait(zx::time deadline) = 0;

  virtual std::string ShellPrompt() = 0;

  // Invoked after the guest |Realm| has been created but before the guest
  // has been launched.
  //
  // Any vsock ports that are listened on here are guaranteed to be ready to
  // accept connections before the guest attempts to connect to them.
  virtual zx_status_t SetupVsockServices(zx::time deadline, GuestLaunchInfo& guest_launch_info) {
    return ZX_OK;
  }

  async::Loop* GetLoop() { return &loop_; }

  fuchsia::virtualization::HostVsockEndpointPtr vsock_;

 private:
  std::unique_ptr<sys::ServiceDirectory> StartWithRealmBuilder(zx::time deadline,
                                                               GuestLaunchInfo& guest_launch_info);
  std::unique_ptr<sys::ServiceDirectory> StartWithUITestManager(zx::time deadline,
                                                                GuestLaunchInfo& guest_launch_info);

  async::Loop& loop_;
  fuchsia::virtualization::GuestPtr guest_;
  FakeNetstack fake_netstack_;
  fuchsia::virtualization::GuestManagerSyncPtr guest_manager_;
  std::optional<SocketLogger> serial_logger_;
  std::optional<GuestConsole> console_;
  uint32_t guest_cid_;
  // Only one of |ui_test_manager_| and |realm_root_| will be non-null, depending on if graphics
  // APIs are used.
  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<component_testing::RealmRoot> realm_root_;
  // The exposed services directory for the test realm.
  std::unique_ptr<sys::ServiceDirectory> realm_services_;
};

class ZirconEnclosedGuest : public EnclosedGuest {
 public:
  explicit ZirconEnclosedGuest(async::Loop& loop) : EnclosedGuest(loop) {}

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::ZIRCON; }

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override;

 protected:
  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t ShutdownAndWait(zx::time deadline) override;
  std::string ShellPrompt() override { return "$ "; }
};

class DebianEnclosedGuest : public EnclosedGuest {
 public:
  explicit DebianEnclosedGuest(async::Loop& loop) : EnclosedGuest(loop) {}

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override;

 protected:
  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t ShutdownAndWait(zx::time deadline) override;
  std::string ShellPrompt() override { return "$ "; }
};

class TerminaEnclosedGuest : public EnclosedGuest {
 public:
  explicit TerminaEnclosedGuest(async::Loop& loop)
      : TerminaEnclosedGuest(loop, fuchsia::virtualization::ContainerStatus::STARTING_VM) {}

  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;
  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env, zx::time deadline,
                      std::string* result, int32_t* return_code) override;

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override;
  void InstallInRealm(component_testing::Realm& realm, GuestLaunchInfo& guest_launch_info) override;

 protected:
  TerminaEnclosedGuest(async::Loop& loop, fuchsia::virtualization::ContainerStatus target_status)
      : EnclosedGuest(loop), target_status_(target_status), executor_(loop.dispatcher()) {}

  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t ShutdownAndWait(zx::time deadline) override;
  std::string ShellPrompt() override { return "$ "; }

 private:
  const fuchsia::virtualization::ContainerStatus target_status_;
  std::unique_ptr<vsh::BlockingCommandRunner> command_runner_;
  async::Executor executor_;
};

class TerminaContainerEnclosedGuest : public TerminaEnclosedGuest {
 public:
  explicit TerminaContainerEnclosedGuest(async::Loop& loop)
      : TerminaEnclosedGuest(loop, fuchsia::virtualization::ContainerStatus::READY) {}

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override;
  void InstallInRealm(component_testing::Realm& realm, GuestLaunchInfo& guest_launch_info) override;
  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env, zx::time deadline,
                      std::string* result, int32_t* return_code) override;
};

using AllGuestTypes =
    ::testing::Types<ZirconEnclosedGuest, DebianEnclosedGuest, TerminaEnclosedGuest>;

class GuestTestNameGenerator {
 public:
  template <typename T>
  static std::string GetName(int idx) {
    // Use is_base_of because some tests will use sub-classes. By default gtest will just use
    // idx to string, so we just suffix the actual enclosed guest type.
    if (std::is_base_of<ZirconEnclosedGuest, T>())
      return std::to_string(idx) + "_ZirconGuest";
    if (std::is_base_of<DebianEnclosedGuest, T>())
      return std::to_string(idx) + "_DebianGuest";
    if (std::is_base_of<TerminaContainerEnclosedGuest, T>())
      return std::to_string(idx) + "_TerminaContainerGuest";
    if (std::is_base_of<TerminaEnclosedGuest, T>())
      return std::to_string(idx) + "_TerminaGuest";
  }
};

#endif  // SRC_VIRTUALIZATION_TESTS_LIB_ENCLOSED_GUEST_H_
