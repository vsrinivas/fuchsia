// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_REALM_BUILDER_H_
#define LIB_SYS_CPP_TESTING_REALM_BUILDER_H_

#include <fuchsia/io2/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/internal/mock_runner.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/testing/scoped_child.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <src/lib/fxl/macros.h>

#include "lib/async/dispatcher.h"

namespace sys::testing {

// A Realm is a subtree of the a component instance. This library allows users
// to create realms at runtime in an idiomatic and ergonomic way. Users can
// create arbitrarily deep subtrees, and route capabilities between any of the
// created components.
// For more information about RealmBuilder, see the following link.
// https://fuchsia.dev/fuchsia-src/development/components/v2/realm_builder
// For examples on how to use this library, see the integration tests
// found at //sdk/cpp/tests/realm_builder_test.cc
class Realm {
 public:
  Realm(Realm&& other) = default;
  Realm& operator=(Realm&& other) = default;

  FXL_DISALLOW_COPY_AND_ASSIGN(Realm);

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

  // Get the child name of the root component.
  std::string GetChildName() const;

  class Builder;

 private:
  explicit Realm(ScopedChild root, std::unique_ptr<internal::MockRunner> mock_runner);

  ScopedChild root_;
  std::unique_ptr<internal::MockRunner> mock_runner_;
};

// A builder class for a Realm object. Use this class to construct a Realm.
class Realm::Builder {
 public:
  // Factory method to create a new RealmBuilder object.
  // |context| must not be NULL and must outlive the RealmBuilder object and
  // created Realm object.
  static Builder New(const sys::ComponentContext* context);

  Builder(Builder&&) = default;
  Builder& operator=(Builder&&) = default;

  FXL_DISALLOW_COPY_AND_ASSIGN(Builder);

  // Insert a component at the specified moniker. If a component already exists
  // at the provided moniker, this method will panic. Furthermore, this
  // method will create all parts of the moniker if non-existent. For example,
  // given the moniker `foo/bar`. The library will create a component at path
  // `foo` before adding its child `bar`.
  Builder& AddComponent(Moniker moniker, Component component);

  // Route a capability from one component to another.
  Builder& AddRoute(CapabilityRoute route);

  // Build the Realm object add prepared by the associated builder methods,
  // e.g. |AddComponent|.
  // |dispatcher| must be non-null, or |async_get_default_dispatcher| must be
  // configured to return a non-null value
  // This function can only be called once per RealmBuilder instance.
  // Multiple invocations will result in a panic.
  Realm Build(async_dispatcher* dispatcher = nullptr);

 private:
  Builder(fuchsia::sys2::RealmSyncPtr realm_proxy,
          fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy,
          ServiceDirectory framework_intermediary_exposed_dir,
          std::unique_ptr<internal::MockRunner> mock_runner);

  bool realm_commited_;
  fuchsia::sys2::RealmSyncPtr realm_proxy_;
  fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy_;
  ServiceDirectory framework_intermediary_exposed_dir_;
  std::unique_ptr<internal::MockRunner> mock_runner_;
};

}  // namespace sys::testing

#endif  // LIB_SYS_CPP_TESTING_REALM_BUILDER_H_
