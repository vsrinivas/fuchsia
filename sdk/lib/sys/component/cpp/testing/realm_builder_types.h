// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_

#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// This file contains structs used by the RealmBuilder library to create realms.

namespace component_testing {

using DependencyType = fuchsia::component::decl::DependencyType;

// A protocol capability. The name refers to the name of the FIDL protocol,
// e.g. `fuchsia.logger.LogSink`.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/protocol.
struct Protocol final {
  std::string_view name;
  cpp17::optional<std::string_view> as = cpp17::nullopt;
  cpp17::optional<DependencyType> type = cpp17::nullopt;
  cpp17::optional<std::string_view> path = cpp17::nullopt;
};

// A service capability. The name refers to the name of the FIDL service,
// e.g. `fuchsia.examples.EchoService`.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/service.
struct Service final {
  std::string_view name;
  cpp17::optional<std::string_view> as = cpp17::nullopt;
  cpp17::optional<std::string_view> path = cpp17::nullopt;
};

// A directory capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/directory.
struct Directory final {
  std::string_view name;
  cpp17::optional<std::string_view> as = cpp17::nullopt;
  cpp17::optional<DependencyType> type = cpp17::nullopt;
  cpp17::optional<std::string_view> subdir = cpp17::nullopt;
  cpp17::optional<fuchsia::io::Operations> rights = cpp17::nullopt;
  cpp17::optional<std::string_view> path = cpp17::nullopt;
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

// A type that specifies the content of a binary file for
// |Realm.RouteReadOnlyDirectory|.
struct BinaryContents {
  // Pointer to bytes of content.
  const void* buffer;

  // Size of content. Only bytes up to this size will be stored.
  size_t size;

  // Offset (optional) to start writing content from |buffer|.
  size_t offset = 0;
};

// An in-memory directory passed to |Realm.RouteReadOnlyDirectory| to
// create directories with files at runtime.
//
// This is useful if a test needs to configure the content of a Directory
// capability provided to a component under test in a Realm.
class DirectoryContents {
 public:
  DirectoryContents() = default;

  // Add a file to this directory with |contents| at destination |path|.
  // Paths can include slashes, e.g. "foo/bar.txt".  However, neither a leading
  // nor a trailing slash must be supplied.
  DirectoryContents& AddFile(std::string_view path, BinaryContents contents);

  // Same as above but allows for a string type for the contents.
  DirectoryContents& AddFile(std::string_view path, std::string_view contents);

 private:
  // Friend class needed in order to invoke |TakeAsFidl|.
  friend class Realm;

  // Take this object and convert it to its FIDL counterpart. Invoking this method
  // resets this object, erasing all previously-added file entries.
  fuchsia::component::test::DirectoryContents TakeAsFidl();

  fuchsia::component::test::DirectoryContents contents_;
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
