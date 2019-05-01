// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GUEST_TESTING_FAKE_ENVIRONMENT_MANAGER_H_
#define LIB_GUEST_TESTING_FAKE_ENVIRONMENT_MANAGER_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/guest/testing/fake_guest_vsock.h>
#include <lib/guest/testing/fake_host_vsock.h>

namespace guest {
namespace testing {

// Provides an implementation of |fuchsia::guest::EnvironmentManager| that
// can create a single Environment/Guest. This is intended to make testing the
// common case of a single component creating a single guest.
class FakeEnvironmentManager : public fuchsia::guest::EnvironmentManager,
                               public fuchsia::guest::EnvironmentController,
                               public fuchsia::guest::InstanceController {
 public:
  FakeGuestVsock* GuestVsock() { return &guest_vsock_; }

  fidl::InterfaceRequestHandler<fuchsia::guest::EnvironmentManager>
  GetHandler() {
    return environment_manager_bindings_.GetHandler(this);
  }

 private:
  // |fuchsia::guest::EnvironmentManager|
  void Create(fidl::StringPtr label,
              fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> env)
      override;
  void List(ListCallback callback) override;
  void Connect(uint32_t id,
               fidl::InterfaceRequest<fuchsia::guest::EnvironmentController>
                   env) override;

  // |fuchsia::guest::EnvironmentController|
  void LaunchInstance(
      fuchsia::guest::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller,
      LaunchInstanceCallback callback) override;
  void ListInstances(ListInstancesCallback callback) override;
  void ConnectToInstance(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller)
      override;
  void ConnectToBalloon(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::guest::BalloonController> controller)
      override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint)
      override;

  // |fuchsia::guest::InstanceController|
  void GetSerial(GetSerialCallback callback) override;

  FakeHostVsock host_vsock_{&guest_vsock_};
  FakeGuestVsock guest_vsock_{&host_vsock_};
  fidl::BindingSet<fuchsia::guest::EnvironmentManager>
      environment_manager_bindings_;
  fidl::Binding<fuchsia::guest::EnvironmentController>
      environment_controller_binding_{this};
  fidl::Binding<fuchsia::guest::InstanceController>
      instance_controller_binding_{this};
};

}  // namespace testing
}  // namespace guest

#endif  // LIB_GUEST_TESTING_FAKE_ENVIRONMENT_MANAGER_H_
