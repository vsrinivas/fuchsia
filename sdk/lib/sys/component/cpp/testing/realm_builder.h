// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/component/cpp/testing/internal/local_component_runner.h>
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

namespace component_testing {

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
// https://fuchsia.dev/fuchsia-src/development/testing/components/realm_builder
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

  // Create a sub realm as child of this Realm instance. The constructed
  // Realm is returned.
  Realm AddChildRealm(const std::string& child_name, ChildOptions options = kDefaultChildOptions);

  // Route a capability from one child to another.
  Realm& AddRoute(Route route);

  /// Offers a directory capability to a component in this realm. The
  /// directory will be read-only (i.e. have `r*` rights), and will have the
  /// contents described in `directory`.
  Realm& RouteReadOnlyDirectory(const std::string& name, std::vector<Ref> to,
                                DirectoryContents directory);

  friend class RealmBuilder;

 private:
  explicit Realm(fuchsia::component::test::RealmSyncPtr realm_proxy,
                 std::shared_ptr<internal::LocalComponentRunner::Builder> runner_builder,
                 std::vector<std::string> scope = {});

  std::string GetResolvedName(const std::string& child_name);

  fuchsia::component::test::RealmSyncPtr realm_proxy_;
  std::shared_ptr<internal::LocalComponentRunner::Builder> runner_builder_;
  std::vector<std::string> scope_;
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

  // Create a sub realm as child of the root realm. The constructed
  // Realm is returned.
  // See |Realm.AddChildRealm| for more details.
  Realm AddChildRealm(const std::string& child_name, ChildOptions options = kDefaultChildOptions);

  // Route a capability for the root realm being constructed.
  // See |Realm.AddRoute| for more details.
  RealmBuilder& AddRoute(Route route);

  /// Offers a directory capability to a component in this realm.
  // See |Realm.RouteReadOnlyDirectory| for more details.
  RealmBuilder& RouteReadOnlyDirectory(const std::string& name, std::vector<Ref> to,
                                       DirectoryContents directory);

  // Build the realm root prepared by the associated builder methods, e.g. |AddComponent|.
  // |dispatcher| must be non-null, or |async_get_default_dispatcher| must be
  // configured to return a non-null value
  // This function can only be called once per Realm::Builder instance.
  // Multiple invocations will result in a panic.
  // |dispatcher| must outlive the lifetime of the constructed |RealmRoot|.
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

}  // namespace component_testing

// Until all clients of the API have been migrated, keep the legacy namespace.
// TODO(fxbug.dev/90794): Remove this.
namespace sys {
namespace testing {
namespace experimental {
using component_testing::Realm;
using component_testing::RealmBuilder;
using component_testing::RealmRoot;
}  // namespace experimental
}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_H_
