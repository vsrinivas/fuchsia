// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_
#define LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_

#include <fuchsia/component/config/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// This file contains structs used by the RealmBuilder library to create realms.

namespace component_testing {

class LocalComponentImpl;

namespace internal {
class LocalComponentInstance;
class LocalComponentRunner;
}  // namespace internal

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

// A storage capability.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities/storage.
struct Storage final {
  std::string_view name;
  cpp17::optional<std::string_view> as = cpp17::nullopt;
  cpp17::optional<std::string_view> path = cpp17::nullopt;
};

// A capability to be routed from one component to another.
// See: https://fuchsia.dev/fuchsia-src/concepts/components/v2/capabilities
using Capability = cpp17::variant<Protocol, Service, Directory, Storage>;

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
  friend LocalComponentImpl;
  friend internal::LocalComponentInstance;
  friend internal::LocalComponentRunner;

  // Called by LocalComponentImpl::Exit().
  void Exit(zx_status_t return_code = ZX_OK);

  fit::function<void(zx_status_t)> on_exit_;
  fdio_ns_t* namespace_;
  sys::OutgoingDirectory outgoing_dir_;
  // [END_EXCLUDE]
};
// [END mock_handles_cpp]

// [START mock_interface_cpp]
// The interface for backing implementations of components with a Source of Mock.
class LocalComponentImpl {
 public:
  virtual ~LocalComponentImpl();

  // Invoked when the Component Manager issues a Start request to the component.
  // |mock_handles| contains the outgoing directory and namespace of
  // the component.
  virtual void OnStart() = 0;

  // The LocalComponentImpl derived class may override this method to be informed if
  // ComponentController::Stop() was called on the controller associated with
  // the component instance. The ComponentController binding will be dropped
  // automatically, immediately after LocalComponentImpl::OnStop() returns.
  virtual void OnStop() {}

  // The component can call this method to terminate its instance. This will
  // release the handles, and drop the |ComponentController|, informing
  // component manager that the component has stopped. Calling |Exit()| will
  // also cause the Realm to drop the |LocalComponentImpl|, which should
  // destruct the component, and the handles and bindings held by the component.
  // Therefore the |LocalComponentImpl| should not do anything else after
  // calling |Exit()|.
  //
  // This method is not valid until |OnStart()| is invoked.
  void Exit(zx_status_t return_code = ZX_OK);

  // Returns the namespace provided to the mock component.
  //
  // This method is not valid until |OnStart()| is invoked.
  fdio_ns_t* ns();

  // Returns a wrapper around the component's outgoing directory. The mock
  // component may publish capabilities using the returned object.
  //
  // This method is not valid until |OnStart()| is invoked.
  sys::OutgoingDirectory* outgoing();

  // Convenience method to construct a ServiceDirectory by opening a handle to
  // "/svc" in the namespace object returned by `ns()`.
  //
  // This method is not valid until |OnStart()| is invoked.
  sys::ServiceDirectory svc();

 private:
  friend internal::LocalComponentRunner;
  // The |LocalComponentHandles| are set by the |LocalComponentRunner| after
  // construction by the factory, and before calling |OnStart()|
  std::unique_ptr<LocalComponentHandles> handles_;
};
// [END mock_interface_cpp]

// The use of this class is DEPRECATED.
//
// The interface for backing implementations of components with a Source of Mock
// when added by deprecated method AddLocalChild(..., LocalComponent*, ...).
//
// TODO(fxbug.dev/109804): Remove this class when all uses have migrated to
// `AddLocalChild(..., LocalComponentFactory, ...)`.
class LocalComponent {
 public:
  virtual ~LocalComponent();

  // Invoked when the Component Manager issues a Start request to the component.
  // |mock_handles| contains the outgoing directory and namespace of
  // the component.
  virtual void Start(std::unique_ptr<LocalComponentHandles> mock_handles) = 0;
};

// Type for a function that returns a new |LocalComponentImpl| when component
// manager requests a new component instance.
//
// See |Realm.AddLocalChild| for more details.
using LocalComponentFactory = fit::function<std::unique_ptr<LocalComponentImpl>()>;

// Type for either variation of implementation passed to AddLocalChild(): the
// deprecated raw pointer, or one of the valid callback functions.
//
// TODO(fxbug.dev/109804): Remove this variant when all uses have migrated to
// `AddLocalChild(..., LocalComponentFactory, ...)`.
using LocalComponentKind = cpp17::variant<LocalComponent*, LocalComponentFactory>;

using StartupMode = fuchsia::component::decl::StartupMode;

struct ChildOptions {
  // Flag used to determine if component should be started eagerly or not.
  // If started eagerly, then it will start as soon as it's resolved.
  // Otherwise, the component will start once another component requests
  // a capability that it offers.
  StartupMode startup_mode = StartupMode::LAZY;

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

// Only valid as the source of a route; routes the capabilities from the framework.
struct FrameworkRef {};

using Ref = cpp17::variant<ParentRef, ChildRef, FrameworkRef>;

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

// Defines a structured configuration value. Used to replace configuration values of existing
// fields of a component.
//
// # Example
//
// ```
// realm_builder.SetConfigValue(echo_server, "echo_string", ConfigValue::String("Hi!"));
// ```
class ConfigValue {
 public:
  ConfigValue() = delete;

  // Implicit type conversion is allowed here to transparently wrap unambiguous types.
  // NOLINTBEGIN(google-explicit-constructor)
  ConfigValue(const char* value);
  ConfigValue(std::string value);
  ConfigValue(std::vector<bool> value);
  ConfigValue(std::vector<uint8_t> value);
  ConfigValue(std::vector<uint16_t> value);
  ConfigValue(std::vector<uint32_t> value);
  ConfigValue(std::vector<uint64_t> value);
  ConfigValue(std::vector<int8_t> value);
  ConfigValue(std::vector<int16_t> value);
  ConfigValue(std::vector<int32_t> value);
  ConfigValue(std::vector<int64_t> value);
  ConfigValue(std::vector<std::string> value);
  // NOLINTEND(google-explicit-constructor)

  ConfigValue(ConfigValue&&) noexcept;
  ConfigValue& operator=(ConfigValue&&) noexcept;
  ConfigValue(const ConfigValue&) = delete;
  ConfigValue& operator=(const ConfigValue&) = delete;
  static ConfigValue Bool(bool value);
  static ConfigValue Uint8(uint8_t value);
  static ConfigValue Uint16(uint16_t value);
  static ConfigValue Uint32(uint32_t value);
  static ConfigValue Uint64(uint64_t value);
  static ConfigValue Int8(int8_t value);
  static ConfigValue Int16(int16_t value);
  static ConfigValue Int32(int32_t value);
  static ConfigValue Int64(int64_t value);

 private:
  // Friend class needed in order to invoke |TakeAsFidl|.
  friend class Realm;

  fuchsia::component::config::ValueSpec TakeAsFidl();
  explicit ConfigValue(fuchsia::component::config::ValueSpec spec);
  fuchsia::component::config::ValueSpec spec;
};

}  // namespace component_testing

#endif  // LIB_SYS_COMPONENT_CPP_TESTING_REALM_BUILDER_TYPES_H_
