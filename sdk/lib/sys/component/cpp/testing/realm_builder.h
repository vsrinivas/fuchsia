// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io2/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/internal/local_component_runner.h>
#include <lib/sys/component/cpp/testing/internal/mock_runner.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/component/cpp/testing/scoped_child.h>
#include <lib/sys/cpp/service_directory.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sys {
namespace testing {

// A Realm is a subtree of the a component instance. This library allows users
// to create realms at runtime in an idiomatic and ergonomic way. Users can
// create arbitrarily deep subtrees, and route capabilities between any of the
// created components.
// For more information about RealmBuilder, see the following link.
// https://fuchsia.dev/fuchsia-src/development/components/v2/realm_builder
// For examples on how to use this library, see the integration tests
// found at //sdk/cpp/tests/realm_builder_test.cc
class Realm final {
 public:
  Realm(Realm&& other) = default;
  Realm& operator=(Realm&& other) = default;

  Realm(const Realm& other) = delete;
  Realm& operator=(const Realm& other) = delete;

  // Connect to an interface in the exposed directory of the root component.
  //
  // The discovery name of the interface is inferred from the C++ type of the
  // interface. Callers can supply an interface name explicitly to override
  // the default name.
  //
  // This overload for |Connect| panics if the connection operation
  // doesn't return ZX_OK. Callers that wish to receive that status should use
  // one of the other overloads that returns a |zx_status_t|.
  //
  // # Example
  //
  // ```
  // auto echo = realm.Connect<test::placeholders::Echo>();
  // ```
  template <typename Interface>
  fidl::InterfacePtr<Interface> Connect(
      const std::string& interface_name = Interface::Name_) const {
    return root_.Connect<Interface>(interface_name);
  }

  // SynchronousInterfacePtr method overload of |Connect|. See
  // method above for more details.
  template <typename Interface>
  fidl::SynchronousInterfacePtr<Interface> ConnectSync(
      const std::string& interface_name = Interface::Name_) const {
    return root_.ConnectSync<Interface>(interface_name);
  }

  // Connect to exposed directory of the root component.
  template <typename Interface>
  zx_status_t Connect(fidl::InterfaceRequest<Interface> request) const {
    return root_.Connect<Interface>(std::move(request));
  }

  // Connect to an interface in the exposed directory using the supplied
  // channel.
  zx_status_t Connect(const std::string& interface_name, zx::channel request) const;

  // Return a handle to the exposed directory of the root component.
  fidl::InterfaceHandle<fuchsia::io::Directory> CloneRoot() const {
    return root_.CloneExposedDir();
  }

  // Get the child name of the root component.
  std::string GetChildName() const;

  class Builder;

 private:
  explicit Realm(ScopedChild root, std::unique_ptr<internal::MockRunner> mock_runner);

  ScopedChild root_;
  std::unique_ptr<internal::MockRunner> mock_runner_;
};

// A builder class for a Realm object. Use this class to construct a Realm.
class Realm::Builder final {
 public:
  // Factory method to create a new RealmBuilder object.
  // |svc| must outlive the RealmBuilder object and created Realm object.
  // If it's nullptr, then the current process' "/svc" namespace entry is used.
  static Builder Create(std::shared_ptr<sys::ServiceDirectory> svc = nullptr);

  Builder(Builder&&) = default;
  Builder& operator=(Builder&&) = default;

  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;

  // Insert a component at the specified moniker. If a component already exists
  // at the provided moniker, this method will panic. Furthermore, this
  // method will create all parts of the moniker if non-existent. For example,
  // given the moniker `foo/bar`. The library will create a component at path
  // `foo` before adding its child `bar`.
  Builder& AddComponent(Moniker moniker, Component component);

  // Route a capability from one component to another.
  Builder& AddRoute(CapabilityRoute route);

  // Build the Realm object prepared by the associated builder methods,
  // e.g. |AddComponent|.
  // |dispatcher| must be non-null, or |async_get_default_dispatcher| must be
  // configured to return a non-null value
  // This function can only be called once per RealmBuilder instance.
  // Multiple invocations will result in a panic.
  Realm Build(async_dispatcher* dispatcher = nullptr);

 private:
  Builder(fuchsia::component::RealmSyncPtr realm_proxy,
          fuchsia::component::test::RealmBuilderSyncPtr realm_builder_proxy,
          ServiceDirectory realm_builder_exposed_dir,
          std::unique_ptr<internal::MockRunner> mock_runner);

  bool realm_commited_;
  fuchsia::component::RealmSyncPtr realm_proxy_;
  fuchsia::component::test::RealmBuilderSyncPtr realm_builder_proxy_;
  ServiceDirectory realm_builder_exposed_dir_;
  std::unique_ptr<internal::MockRunner> mock_runner_;
};

namespace experimental {

// Default child options provided to all components.
const ChildOptions kDefaultChildOptions{.startup_mode = StartupMode::LAZY, .environment = ""};

// Root of a constructed Realm. This object can not be instantiated directly.
// Instead, it can only be constructed with the Realm::Builder/Build().
class RealmRoot final {
 public:
  RealmRoot(RealmRoot&& other) = default;
  RealmRoot& operator=(RealmRoot&& other) = default;

  RealmRoot(RealmRoot& other) = delete;
  RealmRoot& operator=(RealmRoot& other) = delete;

  // Connect to an interface in the exposed directory of the root component.
  //
  // The discovery name of the interface is inferred from the C++ type of the
  // interface. Callers can supply an interface name explicitly to override
  // the default name.
  //
  // This overload for |Connect| panics if the connection operation
  // doesn't return ZX_OK. Callers that wish to receive that status should use
  // one of the other overloads that returns a |zx_status_t|.
  //
  // # Example
  //
  // ```
  // auto echo = realm.Connect<test::placeholders::Echo>();
  // ```
  template <typename Interface>
  fidl::InterfacePtr<Interface> Connect(
      const std::string& interface_name = Interface::Name_) const {
    return root_.Connect<Interface>(interface_name);
  }

  // SynchronousInterfacePtr method overload of |Connect|. See
  // method above for more details.
  template <typename Interface>
  fidl::SynchronousInterfacePtr<Interface> ConnectSync(
      const std::string& interface_name = Interface::Name_) const {
    return root_.ConnectSync<Interface>(interface_name);
  }

  // Connect to exposed directory of the root component.
  template <typename Interface>
  zx_status_t Connect(fidl::InterfaceRequest<Interface> request) const {
    return root_.Connect<Interface>(std::move(request));
  }

  // Connect to an interface in the exposed directory using the supplied
  // channel.
  zx_status_t Connect(const std::string& interface_name, zx::channel request) const;

  // Return a handle to the exposed directory of the root component.
  fidl::InterfaceHandle<fuchsia::io::Directory> CloneRoot() const {
    return root_.CloneExposedDir();
  }

  // Get the child name of the root component.
  std::string GetChildName() const;

 private:
  // Friend classes are needed because the constructor is private.
  friend class Realm;
  friend class RealmBuilder;

  explicit RealmRoot(std::unique_ptr<internal::LocalComponentRunner> local_component_runner,
                     ScopedChild root);

  std::unique_ptr<internal::LocalComponentRunner> local_component_runner_;
  ScopedChild root_;
};

// A `Realm` describes a component instance together with its children.
// Clients can use this class to build a realm from scratch,
// programmatically adding children and routes.
//
// Clients may also use this class to recursively build sub-realms by calling
// `AddChildRealm`.
// For more information about RealmBuilder, see the following link.
// https://fuchsia.dev/fuchsia-src/development/components/v2/realm_builder
// For examples on how to use this library, see the integration tests
// found at //sdk/cpp/tests/realm_builder_test.cc
class Realm final {
 public:
  Realm(Realm&&) = default;
  Realm& operator=(Realm&&) = default;

  Realm(const Realm&) = delete;
  Realm operator=(const Realm&) = delete;

  // Add a v2 component (.cm) to this Realm.
  // Names must be unique. Duplicate names will result in a panic.
  Realm& AddChild(const std::string& child_name, const std::string& url,
                  ChildOptions options = kDefaultChildOptions);

  // Add a v1 component (.cmx) to this Realm.
  // Names must be unique. Duplicate names will result in a panic.
  Realm& AddLegacyChild(const std::string& child_name, const std::string& url,
                        ChildOptions options = kDefaultChildOptions);

  // Add a component implemented by a C++ class. The class should inherit from
  // the `Local` class. The pointer must not be nullptr. If it is, this
  // method will panic. It is expected the pointer will be valid for the lifetime
  // of the constructed RealmRoot below.
  // Names must be unique. Duplicate names will result in a panic.
  Realm& AddLocalChild(const std::string& child_name, LocalComponent* local_impl,
                       ChildOptions options = kDefaultChildOptions);

  // Route a capability from one child to another.
  Realm& AddRoute(Route route);

  friend class RealmBuilder;

 private:
  explicit Realm(fuchsia::component::test::RealmSyncPtr realm_proxy,
                 std::shared_ptr<internal::LocalComponentRunner::Builder> local_component_runner);

  fuchsia::component::test::RealmSyncPtr realm_proxy_;
  std::shared_ptr<internal::LocalComponentRunner::Builder> runner_builder_;
};

// Use this Builder class to construct a Realm object.
class RealmBuilder final {
 public:
  // Factory method to create a new Realm::Builder object.
  // |svc| must outlive the RealmBuilder object and created Realm object.
  // If it's nullptr, then the current process' "/svc" namespace entry is used.
  static RealmBuilder Create(std::shared_ptr<sys::ServiceDirectory> svc = nullptr);

  RealmBuilder(RealmBuilder&&) = default;
  RealmBuilder& operator=(RealmBuilder&&) = default;

  RealmBuilder(const RealmBuilder&) = delete;
  RealmBuilder& operator=(const RealmBuilder&) = delete;

  // Add a v2 component (.cm) to the root realm being constructed.
  // See |Realm.AddChild| for more details.
  RealmBuilder& AddChild(const std::string& child_name, const std::string& url,
                         ChildOptions options = kDefaultChildOptions);

  // Add a v1 component (.cmx) to the root realm being constructed.
  // See |Realm.AddLegacyChild| for more details.
  RealmBuilder& AddLegacyChild(const std::string& child_name, const std::string& url,
                               ChildOptions options = kDefaultChildOptions);

  // Add a component implemented by a C++ class.
  // See |Realm.AddLocalChild| for more details.
  RealmBuilder& AddLocalChild(const std::string& child_name, LocalComponent* local_impl,
                              ChildOptions options = kDefaultChildOptions);

  // Route a capability for the root realm being constructed.
  // See |Realm.AddRoute| for more details.
  RealmBuilder& AddRoute(Route route);

  // Build the realm root prepared by the associated builder methods, e.g. |AddComponent|.
  // |dispatcher| must be non-null, or |async_get_default_dispatcher| must be
  // configured to return a non-null value
  // This function can only be called once per Realm::Builder instance.
  // Multiple invocations will result in a panic.
  RealmRoot Build(async_dispatcher* dispatcher = nullptr);

  // A reference to the root `Realm` object.
  Realm& root();

 private:
  RealmBuilder(std::shared_ptr<sys::ServiceDirectory> svc,
               fuchsia::component::test::BuilderSyncPtr builder_proxy,
               fuchsia::component::test::RealmSyncPtr test_realm_proxy);

  bool realm_commited_ = false;
  std::shared_ptr<sys::ServiceDirectory> svc_;
  fuchsia::component::test::BuilderSyncPtr builder_proxy_;
  std::shared_ptr<internal::LocalComponentRunner::Builder> runner_builder_;
  Realm root_;
};

}  // namespace experimental

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_
