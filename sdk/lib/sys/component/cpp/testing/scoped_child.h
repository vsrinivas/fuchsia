// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_SCOPED_CHILD_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_SCOPED_CHILD_H_

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>

namespace component_testing {

// A scoped instance of a dynamically created child component. This class
// will automatically destroy the child component once it goes out of scope.
class ScopedChild final {
 public:
  // Create a dynamic child component using the fuchsia.component.Realm API.
  // |realm_proxy| must be bound to a connection to the fuchsia.component.Realm protocol.
  // |collection| is the name of the collection to create the child under. This
  // field must refer to a name in the current component's manifest file.
  // |name| is the name to assign to the child.
  // |url| is the component component URL of the child component.
  static ScopedChild New(fuchsia::component::RealmSyncPtr realm_proxy, std::string collection,
                         std::string name, std::string url);

  // Same as above with a randomly generated `name`.
  static ScopedChild New(fuchsia::component::RealmSyncPtr realm_proxy, std::string collection,
                         std::string url);

  // Create a dynamic child component using the fuchsia.component.Realm API.
  // |collection| is the name of the collection to create the child under. This
  // field must refer to a name in the current component's manifest file.
  // |name| is the name to assign to the child.
  // |url| is the component component URL of the child component.
  // |svc| is used to make a connection to the protocol. If it's not provided,
  // then the namespace entry will be used.
  static ScopedChild New(std::string collection, std::string name, std::string url,
                         std::shared_ptr<sys::ServiceDirectory> svc = nullptr);

  // Same as above with a randomly generated `name`.
  static ScopedChild New(std::string collection, std::string url,
                         std::shared_ptr<sys::ServiceDirectory> svc = nullptr);

  ~ScopedChild();

  ScopedChild(ScopedChild&&) noexcept;
  ScopedChild& operator=(ScopedChild&&) noexcept;

  ScopedChild(const ScopedChild&) = delete;
  ScopedChild& operator=(const ScopedChild&) = delete;

  // When the destructor of this object is invoked, call
  // fuchsia.component/Realm.DestroyChild asynchronously. This will make the
  // operation non-blocking, which is useful if a test has a slow running
  // teardown or if destruction *must* be async for any other reason.
  // |dispatcher| must be non-null, or |async_get_default_dispatcher| must be
  // configured to return a non-null value
  // |dispatcher| must outlive the lifetime of this object.
  void MakeTeardownAsync(async_dispatcher_t* dispatcher = nullptr);

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

  // Clone the exposed directory.
  fidl::InterfaceHandle<fuchsia::io::Directory> CloneExposedDir() const {
    return exposed_dir_.CloneChannel();
  }

 private:
  ScopedChild(std::shared_ptr<sys::ServiceDirectory> svc,
              fuchsia::component::decl::ChildRef child_ref, sys::ServiceDirectory exposed_dir);

  std::shared_ptr<sys::ServiceDirectory> svc_ = nullptr;
  fuchsia::component::decl::ChildRef child_ref_;
  sys::ServiceDirectory exposed_dir_;
  async_dispatcher_t* dispatcher_ = nullptr;
  bool has_moved_ = false;
};

}  // namespace component_testing

// Until all clients of the API have been migrated, keep the legacy namespace.
// TODO(fxbug.dev/90794): Remove this.
namespace sys {
namespace testing {
using component_testing::ScopedChild;
}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_SCOPED_CHILD_H_
