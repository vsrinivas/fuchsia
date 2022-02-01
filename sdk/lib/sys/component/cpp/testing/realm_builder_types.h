// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_

#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <string>
#include <string_view>
#include <variant>

// This file contains structs used by the RealmBuilder library to create realms.

namespace component_testing {

// A protocol capability. The name refers to the name of the FIDL protocol,
// e.g. `fuchsia.logger.LogSink`.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/protocol.
struct Protocol final {
  std::string_view name;
};

// A service capability. The name refers to the name of the FIDL service,
// e.g. `fuchsia.examples.EchoService`.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/service.
struct Service final {
  std::string_view name;
};

// A directory capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/directory.
struct Directory final {
  std::string_view name;
  std::string_view path;
  fuchsia::io2::Operations rights;
};

// A capability to be routed from one component to another.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities
using Capability = cpp17::variant<Protocol, Service, Directory>;

// [START mock_handles_cpp]
// Handles provided to mock component.
class LocalComponentHandles final {
 public:
  // [START_EXCLUDE]
  LocalComponentHandles(fdio_ns_t* ns, sys::OutgoingDirectory outgoing_dir);
  ~LocalComponentHandles();

  LocalComponentHandles(LocalComponentHandles&&) noexcept;
  LocalComponentHandles& operator=(LocalComponentHandles&&) noexcept;

  LocalComponentHandles(LocalComponentHandles&) = delete;
  LocalComponentHandles& operator=(LocalComponentHandles&) = delete;
  // [END_EXCLUDE]

  // Returns the namespace provided to the mock component. The returned pointer
  // will be invalid once *this is destroyed.
  fdio_ns_t* ns();

  // Returns a wrapper around the component's outgoing directory. The mock
  // component may publish capabilities using the returned object. The returned
  // pointer will be invalid once *this is destroyed.
  sys::OutgoingDirectory* outgoing();

  // Convenience method to construct a ServiceDirectory by opening a handle to
  // "/svc" in the namespace object returned by `ns()`.
  sys::ServiceDirectory svc();

  // [START_EXCLUDE]
 private:
  fdio_ns_t* namespace_;
  sys::OutgoingDirectory outgoing_dir_;
  // [END_EXCLUDE]
};
// [END mock_handles_cpp]

// [START mock_interface_cpp]
// The interface for backing implementations of components with a Source of Mock.
class LocalComponent {
 public:
  virtual ~LocalComponent();

  // Invoked when the Component Manager issues a Start request to the component.
  // |mock_handles| contains the outgoing directory and namespace of
  // the component.
  virtual void Start(std::unique_ptr<LocalComponentHandles> mock_handles);
};
// [END mock_interface_cpp]

using StartupMode = fuchsia::component::decl::StartupMode;

struct ChildOptions {
  // Flag used to determine if component should be started eagerly or not.
  // If started eagerly, then it will start as soon as it's resolved.
  // Otherwise, the component will start once another component requests
  // a capability that it offers.
  StartupMode startup_mode;

  // Set the environment for this child to run in. The environment specified
  // by this field must already exist by the time this is set.
  // Otherwise, calls to AddChild will panic.
  std::string_view environment;
};

// If this is used for the root Realm, then this endpoint refers to the test
// component itself. This used to route capabilities to/from the test component.
// If this ise used in a sub Realm, then `Parent` will refer to its parent Realm.
struct ParentRef {};

struct ChildRef {
  std::string_view name;
};

using Ref = cpp17::variant<ParentRef, ChildRef>;

struct Route {
  std::vector<Capability> capabilities;
  Ref source;
  std::vector<Ref> targets;
};

}  // namespace component_testing

// Until all clients of the API have been migrated, keep the legacy namespace.
// TODO(fxbug.dev/90794): Remove this.
namespace sys {
namespace testing {
using component_testing::Capability;
using component_testing::ChildOptions;
using component_testing::ChildRef;
using component_testing::Directory;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Ref;
using component_testing::Route;
using component_testing::Service;
using component_testing::StartupMode;
}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_
