// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_REALM_BUILDER_TYPES_H_
#define LIB_SYS_CPP_TESTING_REALM_BUILDER_TYPES_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/include/lib/fdio/namespace.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>
#include <string_view>
#include <variant>

// This file contains structs used by the RealmBuilder library to create realms.

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
  std::string_view path;
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
  std::string_view name;
};

// A directory capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/directory.
struct Directory {
  std::string_view name;
  std::string_view path;
  fuchsia::io2::Operations rights;
};

// A storage capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/storage.
struct Storage {
  std::string_view name;
  std::string_view path;
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
  std::string_view url;
};

// A reference to a component via its legacy component URL.
// For example, `fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx`.
struct LegacyComponentUrl {
  std::string_view url;
};

// Handles provided to mock component.
class MockHandles {
 public:
  MockHandles(fdio_ns_t* ns, OutgoingDirectory outgoing_dir);
  ~MockHandles();

  MockHandles(MockHandles&&) noexcept;
  MockHandles& operator=(MockHandles&&) noexcept;

  MockHandles(MockHandles&) = delete;
  MockHandles& operator=(MockHandles&) = delete;

  // Returns the namespace provided to the mock component. The returned pointer
  // will be invalid once *this is destroyed.
  fdio_ns_t* ns();

  // Returns a wrapper around the component's outgoing directory. The mock
  // component may publish capabilities using the returned object. The returned
  // pointer will be invalid once *this is destroyed.
  OutgoingDirectory* outgoing();

  // Convenience method to construct a ServiceDirectory by opening a handle to
  // "/svc" in the namespace object returned by `ns()`.
  ServiceDirectory svc();

 private:
  fdio_ns_t* namespace_;
  OutgoingDirectory outgoing_dir_;
};

// The interface for backing implementations of components with a Source of Mock.
class MockComponent {
 public:
  // Invoked when the Component Manager issues a Start request to the component.
  // |mock_handles| contains the outgoing directory and namespace of
  // the component.
  virtual void Start(std::unique_ptr<MockHandles> mock_handles) = 0;
};

// A reference to a mock component.
struct Mock {
  MockComponent* impl;
};

// The source of a component. If it's `ComponentUrl`, then it will be located
// via its component URL.
using Source = std::variant<ComponentUrl, LegacyComponentUrl, Mock>;

// A component as referred to by its source.
struct Component {
  Source source;
  // Flag used to determine if component should be started eagerly or not.
  // If started eagerly, then it will start as soon as it's resolved.
  // Otherwise, the component will start once another component requests
  // a capability that it offers.
  bool eager = false;
};

}  // namespace sys::testing

#endif  // LIB_SYS_CPP_TESTING_REALM_BUILDER_TYPES_H_
