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
#include <lib/sys/cpp/testing/internal/scoped_instance.h>
#include <zircon/system/public/zircon/types.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include <src/lib/fxl/macros.h>

namespace sys::testing {

// A moniker identifies a specific component instance in the component tree
// using a topological path. For example, given the following component tree:
//   <root>
//    / \
//   a   b
//  /
// c
// Where components "a" and "b" are direct children of the root, and "c" is the
// only grandchild of the root, the following monikers are valid:
//
// '' (empty string) to refer to the root component.
// 'a' and 'b' to refer to the children of the root
// 'a/c' to refer to component "c".
//
// There is no leading slash.
struct Moniker {
  std::string path;
};

// Endpoint to root above the created Realm. This endpoint is used to route
// capabilities from/to client of RealmBuilder.
struct AboveRoot {};

// An endpoint refers to either a source or target when routing a capability.
using Endpoint = std::variant<AboveRoot, Moniker>;

// A protocol capability. The name refers to the name of the FIDL protocol,
// e.g. `fuchsia.logger.LogSink`.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/protocol.
struct Protocol {
  std::string name;
};

// A directory capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/directory.
struct Directory {
  std::string name;
  std::string path;
  fuchsia::io2::Operations rights;
};

// A storage capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/storage.
struct Storage {
  std::string name;
  std::string path;
};

// A capability to be routed from one component to another.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities
using Capability = std::variant<Protocol, Directory, Storage>;

// A routing of a capability from source to multiple targets.
struct CapabilityRoute {
  Capability capability;
  Endpoint source;
  std::vector<Endpoint> targets;
};

// A reference to a component via its component URL.
// For example, `fuchsia-pkg://fuchsia.com/foo#meta/bar.cm`.
struct ComponentUrl {
  std::string url;
};

// A reference to a component via its legacy component URL.
// For example, `fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx`.
struct LegacyComponentUrl {
  std::string url;
};

// The source of a component. If it's `ComponentUrl`, then it will be located
// via its component URL.
using Source = std::variant<ComponentUrl, LegacyComponentUrl>;

// A component as referred to by its source.
struct Component {
  Source source;
  // Flag used to determine if component should be started eagerly or not.
  // If started eagerly, then it will start as soon as it's resolved.
  // Otherwise, the component will start once another component requests
  // a capability that it offers.
  bool eager = false;
};

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

  // Connect to exposed directory of the root component.
  template <typename Interface>
  zx_status_t Connect(fidl::InterfaceRequest<Interface> request) const {
    return root_.ConnectAtExposedDir<Interface>(std::move(request));
  }

  class Builder;

 private:
  explicit Realm(internal::ScopedInstance root);

  internal::ScopedInstance root_;
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
  // |context| must not be NULL and must outlive the lifetime of the created
  // Realm object.
  // This function can only be called once per RealmBuilder instance.
  // Multiple invocations will result in a panic.
  Realm Build(const sys::ComponentContext* context);

 private:
  Builder(fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy,
          ServiceDirectory framework_intermediary_exposed_dir);

  bool realm_commited_;
  fuchsia::realm::builder::FrameworkIntermediarySyncPtr framework_intermediary_proxy_;
  ServiceDirectory framework_intermediary_exposed_dir_;
};

}  // namespace sys::testing

#endif  // LIB_SYS_CPP_TESTING_REALM_BUILDER_H_
