// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_SCOPED_CHILD_H_
#define LIB_SYS_CPP_TESTING_SCOPED_CHILD_H_

#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <src/lib/fxl/macros.h>

namespace sys::testing {

// A scoped instance of a dynamically created child component. This class
// will automatically destroy the child component once it goes out of scope.
class ScopedChild {
 public:
  // Create a dynamic child component using the fuchsia.sys2.Realm API.
  // |realm_proxy| must be bound to a connection to the fuchsia.sys2.Realm protocol.
  // |collection| is the name of the collection to create the child under. This
  // field must refer to a name in the current component's manifest file.
  // |name| is the name to assign to the child.
  // |url| is the component component URL of the child component.
  static ScopedChild New(fuchsia::sys2::RealmSyncPtr realm_proxy, std::string collection,
                         std::string name, std::string url);

  // Same as above with a randomly generated `name`.
  static ScopedChild New(fuchsia::sys2::RealmSyncPtr realm_proxy, std::string collection,
                         std::string url);

  ~ScopedChild();

  ScopedChild(ScopedChild&&) noexcept;
  ScopedChild& operator=(ScopedChild&&) noexcept;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedChild);

  // Connect to an interface in the exposed directory of the child component.
  //
  // The discovery name of the interface is inferred from the C++ type of the
  // interface. Callers can supply an interface name explicitly to override
  // the default name.
  //
  // This overload for |ConnectAtExposedDir| panics if the connection operation
  // doesn't return ZX_OK. Callers that wish to receive that status should use
  // one of the other overloads that returns a |zx_status_t|.
  //
  // # Example
  //
  // ```
  // auto echo = instance.Connect<test::placeholders::Echo>();
  // ```
  template <typename Interface>
  fidl::InterfacePtr<Interface> Connect(
      const std::string& interface_name = Interface::Name_) const {
    fidl::InterfacePtr<Interface> result;
    zx_status_t status = Connect<Interface>(result.NewRequest());
    ZX_ASSERT_MSG(status == ZX_OK, "Connect to protocol %s on the exposed dir of %s failed: %s",
                  interface_name.c_str(), child_ref_.name.c_str(), zx_status_get_string(status));
    return std::move(result);
  }

  // SynchronousInterfacePtr method overload of |ConnectAtExposedDir|. See
  // method above for more details.
  template <typename Interface>
  fidl::SynchronousInterfacePtr<Interface> ConnectSync(
      const std::string& interface_name = Interface::Name_) const {
    fidl::SynchronousInterfacePtr<Interface> result;
    ZX_ASSERT_MSG(Connect<Interface>(result.NewRequest()) == ZX_OK,
                  "Connect to protocol %s on the exposed dir of %s failed", interface_name.c_str(),
                  child_ref_.name.c_str());
    return std::move(result);
  }

  // Connect to exposed directory of the child component.
  template <typename Interface>
  zx_status_t Connect(fidl::InterfaceRequest<Interface> request) const {
    return exposed_dir_.Connect<Interface>(std::move(request));
  }

  // Connect to an interface in the exposed directory using the supplied
  // channel.
  zx_status_t Connect(const std::string& interface_name, zx::channel request) const;

  // Get the child name of this instance.
  std::string GetChildName() const;

 private:
  explicit ScopedChild(fuchsia::sys2::RealmSyncPtr, fuchsia::sys2::ChildRef child_ref_,
                       ServiceDirectory exposed_dir);

  fuchsia::sys2::RealmSyncPtr realm_proxy_;
  fuchsia::sys2::ChildRef child_ref_;
  ServiceDirectory exposed_dir_;
  bool has_moved_;
};

}  // namespace sys::testing

#endif  // LIB_SYS_CPP_TESTING_SCOPED_CHILD_H_
