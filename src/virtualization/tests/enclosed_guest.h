// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
#define SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "src/virtualization/lib/grpc/grpc_vsock_server.h"
#include "src/virtualization/lib/vsh/command_runner.h"
#include "src/virtualization/packages/biscotti_guest/third_party/protos/vm_guest.grpc.pb.h"
#include "src/virtualization/packages/biscotti_guest/third_party/protos/vm_host.grpc.pb.h"
#include "src/virtualization/tests/fake_netstack.h"
#include "src/virtualization/tests/fake_scenic.h"
#include "src/virtualization/tests/guest_console.h"

static constexpr char kZirconGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cmx";
static constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";
static constexpr char kTerminaGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/termina_guest#meta/termina_guest.cmx";

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
      : loop_(&kAsyncLoopConfigAttachToCurrentThread),
        real_services_(sys::ServiceDirectory::CreateFromNamespace()) {}
  virtual ~EnclosedGuest() {}

  zx_status_t Start();
  void Stop() {
    WaitForSystemStopped();
    loop_.Quit();
  }

  bool Ready() const { return ready_; }

  // Execute |command| on the guest serial and wait for the |result|.
  virtual zx_status_t Execute(const std::vector<std::string>& argv,
                              const std::unordered_map<std::string, std::string>& env = {},
                              std::string* result = nullptr, int32_t* return_code = nullptr);

  // Run a test util named |util| with |argv| in the guest and wait for the
  // |result|.
  zx_status_t RunUtil(const std::string& util, const std::vector<std::string>& argv,
                      std::string* result = nullptr);

  // Return a shell command for a test utility named |util| with the given
  // |argv| in the guest. The result may be passed directly to |Execute|
  // to actually run the command.
  virtual std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                                      const std::vector<std::string>& argv) = 0;

  virtual GuestKernel GetGuestKernel() = 0;

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint) {
    realm_->GetHostVsockEndpoint(std::move(endpoint));
  }

  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> balloon_controller) {
    realm_->ConnectToBalloon(guest_cid_, std::move(balloon_controller));
  }

  uint32_t GetGuestCid() const { return guest_cid_; }

  FakeNetstack* GetNetstack() { return &fake_netstack_; }

  FakeScenic* GetScenic() { return &fake_scenic_; }

  GuestConsole* GetConsole() { return console_.get(); }

 protected:
  // Provides guest specific |launch_info|, called by Start.
  virtual zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) = 0;

  // Waits until the guest is ready to run test utilities, called by Start.
  virtual zx_status_t WaitForSystemReady() = 0;

  virtual std::string ShellPrompt() = 0;

  // Waits for the guest to perform an graceful shutdown operations.
  virtual void WaitForSystemStopped() {}

  // Invoked after the guest |Realm| has been created but before the guest
  // has been launched.
  //
  // Any vsock ports that are listened on here are guaranteed to be ready to
  // accept connections before the guest attempts to connect to them.
  virtual zx_status_t SetupVsockServices() { return ZX_OK; }

 protected:
  async::Loop loop_;

 private:
  std::shared_ptr<sys::ServiceDirectory> real_services_;
  fuchsia::sys::EnvironmentPtr real_env_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> enclosing_environment_;
  fuchsia::virtualization::ManagerPtr manager_;
  fuchsia::virtualization::RealmPtr realm_;
  fuchsia::virtualization::GuestPtr guest_;
  FakeScenic fake_scenic_;
  FakeNetstack fake_netstack_;
  std::unique_ptr<GuestConsole> console_;
  uint32_t guest_cid_;
  bool ready_ = false;
};

template <typename EnclosedGuestImpl>
class SingleCpuEnclosedGuest : public EnclosedGuestImpl {
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override {
    zx_status_t status = EnclosedGuestImpl::LaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }
    launch_info->guest_config.set_virtio_gpu(false);
    launch_info->guest_config.set_cpus(1);
    return ZX_OK;
  }
};

class ZirconEnclosedGuest : public EnclosedGuest {
 public:
  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::ZIRCON; }

 protected:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
  std::string ShellPrompt() override { return "$ "; }
};

class DebianEnclosedGuest : public EnclosedGuest {
 public:
  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;

  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

 protected:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
  std::string ShellPrompt() override { return "$ "; }
};

class TerminaEnclosedGuest : public EnclosedGuest, public vm_tools::StartupListener::Service {
 public:
  GuestKernel GetGuestKernel() override { return GuestKernel::LINUX; }

  std::vector<std::string> GetTestUtilCommand(const std::string& util,
                                              const std::vector<std::string>& argv) override;
  zx_status_t Execute(const std::vector<std::string>& argv,
                      const std::unordered_map<std::string, std::string>& env, std::string* result,
                      int32_t* return_code) override;

 protected:
  zx_status_t LaunchInfo(fuchsia::virtualization::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
  void WaitForSystemStopped() override;
  std::string ShellPrompt() override { return "$ "; }

 private:
  zx_status_t SetupVsockServices() override;

  // |vm_tools::StartupListener::Service|
  grpc::Status VmReady(grpc::ServerContext* context, const vm_tools::EmptyMessage* request,
                       vm_tools::EmptyMessage* response) override;

  std::unique_ptr<vsh::BlockingCommandRunner> command_runner_;
  async::Executor executor_{async_get_default_dispatcher()};
  std::unique_ptr<GrpcVsockServer> server_;
  std::unique_ptr<vm_tools::Maitred::Stub> maitred_;
  fuchsia::virtualization::HostVsockEndpointPtr vsock_;
};

// Termina guests should be added here once they're stable in CQ.
using AllGuestTypes =
    ::testing::Types<ZirconEnclosedGuest, SingleCpuEnclosedGuest<ZirconEnclosedGuest>,
                     DebianEnclosedGuest, SingleCpuEnclosedGuest<DebianEnclosedGuest>>;

#endif  // SRC_VIRTUALIZATION_TESTS_ENCLOSED_GUEST_H_
