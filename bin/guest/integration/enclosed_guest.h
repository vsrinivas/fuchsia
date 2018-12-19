// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_ENCLOSED_GUEST_H_
#define GARNET_BIN_GUEST_INTEGRATION_ENCLOSED_GUEST_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/environment_services_helper.h>
#include <lib/component/cpp/testing/test_with_environment.h>

#include "garnet/bin/guest/integration/test_serial.h"

static constexpr char kZirconGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/zircon_guest#meta/zircon_guest.cmx";
static constexpr char kLinuxGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/linux_guest#meta/linux_guest.cmx";

// EnclosedGuest is a base class that defines an guest environment and instance
// encapsulated in an EnclosingEnvironment. A derived class must define the
// |LaunchInfo| to send to the guest environment controller, as well as methods
// for waiting for the guest to be ready and running test utilities. Most tests
// will derive from either ZirconEnclosedGuest or LinuxEnclosedGuest below and
// override LaunchInfo only. EnclosedGuest is designed to be used with
// GuestTest.
class EnclosedGuest {
 public:
  EnclosedGuest()
      : loop_(&kAsyncLoopConfigAttachToThread),
        real_services_(component::GetEnvironmentServices()) {}
  virtual ~EnclosedGuest() {}

  zx_status_t Start();
  void Stop() { loop_.Quit(); }

  bool Ready() const { return ready_; }

  // Execute |command| on the guest serial and wait for the |result|.
  zx_status_t Execute(const std::string& command,
                      std::string* result = nullptr) {
    return serial_.ExecuteBlocking(command, result);
  }

  // Run a test util named |util| with |args| in the guest and wait for the
  // |result|. |args| are specified as a single string with individual arguments
  // separated by spaces, just as you would expect on the command line. The
  // implementation is guest specific.
  virtual zx_status_t RunUtil(const std::string& util, const std::string& args,
                              std::string* result = nullptr) = 0;

  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint) {
    environment_controller_->GetHostVsockEndpoint(std::move(endpoint));
  }

  uint32_t GetGuestCid() const { return guest_cid_; }

 protected:
  // Provides guest specific |launch_info|, called by Start.
  virtual zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) = 0;

  // Waits until the guest is ready to run test utilities, called by Start.
  virtual zx_status_t WaitForSystemReady() = 0;

 private:
  async::Loop loop_;
  std::shared_ptr<component::Services> real_services_;
  fuchsia::sys::EnvironmentPtr real_env_;
  std::unique_ptr<component::testing::EnclosingEnvironment>
      enclosing_environment_;
  fuchsia::guest::EnvironmentManagerPtr environment_manager_;
  fuchsia::guest::EnvironmentControllerPtr environment_controller_;
  fuchsia::guest::InstanceControllerPtr instance_controller_;
  TestSerial serial_;
  uint32_t guest_cid_;
  bool ready_ = false;
};

class ZirconEnclosedGuest : public EnclosedGuest {
 public:
  zx_status_t RunUtil(const std::string& util, const std::string& args,
                      std::string* result = nullptr) override;

 protected:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
};

class LinuxEnclosedGuest : public EnclosedGuest {
 public:
  zx_status_t RunUtil(const std::string& util, const std::string& args,
                      std::string* result = nullptr) override;

 protected:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override;
  zx_status_t WaitForSystemReady() override;
};

#endif  // GARNET_BIN_GUEST_INTEGRATION_ENCLOSED_GUEST_H_