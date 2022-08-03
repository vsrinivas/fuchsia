// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
#define SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <memory>

#include <gtest/gtest.h>

#include "lib/async/dispatcher.h"
#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/lib/vsh/command_runner.h"
#include "src/virtualization/tests/fake_netstack.h"
#include "src/virtualization/tests/fake_scenic.h"
#include "src/virtualization/tests/guest_console.h"
#include "src/virtualization/tests/socket_logger.h"
#include "src/virtualization/third_party/vm_tools/vm_guest.grpc.pb.h"
#include "src/virtualization/third_party/vm_tools/vm_host.grpc.pb.h"

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
  zx_status_t InstallInRealm(component_testing::RealmBuilder& realm_builder,
                             GuestLaunchInfo& guest_launch_info);
  zx_status_t LaunchInRealm(const component_testing::RealmRoot& realm_root,
                            GuestLaunchInfo& guest_launch_info, zx::time deadline);

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

  // Return a shell command for a test utility named |util| with the given
  // |argv| in the guest. The result may be passed directly to |Execute|
  // to actually run the command.
  virtual std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                                      const std::vector<std::string>& argv) = 0;

  virtual GuestKernel GetGuestKernel() = 0;

  void ConnectToBalloon(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::BalloonController> controller);

  void GetHostVsockEndpoint(
      ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint);

  uint32_t GetGuestCid() const { return guest_cid_; }

  FakeNetstack* GetNetstack() { return &fake_netstack_; }

  FakeScenic* GetScenic() { return &fake_scenic_; }

  std::optional<GuestConsole>& GetConsole() { return console_; }

 protected:
  // Provides guest specific launch info, called by Start.
  virtual zx_status_t LaunchInfo(GuestLaunchInfo* launch_info) = 0;

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
  async::Loop& loop_;

  // Can be null if the realm is created externally by the test code.
  std::unique_ptr<component_testing::RealmRoot> realm_root_;

  fuchsia::virtualization::GuestPtr guest_;
  FakeScenic fake_scenic_;
  FakeNetstack fake_netstack_;

  fuchsia::virtualization::GuestManagerSyncPtr guest_manager_;

  std::optional<SocketLogger> serial_logger_;
  std::optional<GuestConsole> console_;
  uint32_t guest_cid_;
};

class ZirconEnclosedGuest : public EnclosedGuest {
 public:
  explicit ZirconEnclosedGuest(async::Loop& loop) : EnclosedGuest(loop) {}

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::ZIRCON; }

 protected:
  zx_status_t LaunchInfo(GuestLaunchInfo* launch_info) override;
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

 protected:
  zx_status_t LaunchInfo(GuestLaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t ShutdownAndWait(zx::time deadline) override;
  std::string ShellPrompt() override { return "$ "; }
};

class TerminaEnclosedGuest : public EnclosedGuest, public vm_tools::StartupListener::Service {
 public:
  explicit TerminaEnclosedGuest(async::Loop& loop)
      : EnclosedGuest(loop), executor_(loop.dispatcher()) {}

  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;
  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env, zx::time deadline,
                      std::string* result, int32_t* return_code) override;

 protected:
  zx_status_t LaunchInfo(GuestLaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady(zx::time deadline) override;
  zx_status_t ShutdownAndWait(zx::time deadline) override;
  std::string ShellPrompt() override { return "$ "; }

 private:
  zx_status_t SetupVsockServices(zx::time deadline, GuestLaunchInfo& guest_launch_info) override;

  // |vm_tools::StartupListener::Service|
  grpc::Status VmReady(grpc::ServerContext* context, const vm_tools::EmptyMessage* request,
                       vm_tools::EmptyMessage* response) override;

  std::unique_ptr<vsh::BlockingCommandRunner> command_runner_;
  async::Executor executor_;
  std::unique_ptr<GrpcVsockServer> server_;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
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
    if (std::is_base_of<TerminaEnclosedGuest, T>())
      return std::to_string(idx) + "_TerminaGuest";
  }
};

#endif  // SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
