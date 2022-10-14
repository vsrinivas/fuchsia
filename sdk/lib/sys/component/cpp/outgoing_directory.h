// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_OUTGOING_DIRECTORY_H_
#define LIB_SYS_COMPONENT_CPP_OUTGOING_DIRECTORY_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/string_view.h>
#include <lib/svc/dir.h>
#include <lib/sys/component/cpp/constants.h>
#include <lib/sys/component/cpp/handlers.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <stack>
#include <type_traits>

namespace component {

// The directory containing handles to capabilities this component provides.
// Entries served from this outgoing directory should correspond to the
// component manifest's `capabilities` declarations.
//
// The outgoing directory contains one special subdirectory, named `svc`. This
// directory contains the FIDL Services and Protocols offered by this component
// to other components. For example the FIDL Protocol `fuchsia.foo.Bar` will be
// hosted under the path `/svc/fuchsia.foo.Bar`.
//
// This class is thread-hostile with respect to its interface. However, its
// |async_dispatcher_t| may be multithreaded as long as it does not service
// requests concurrently with any operations on the object's interface or its
// destruction.
//
// # Simple usage
//
// Instances of this class should be owned and managed on the same thread
// that services their connections.
//
// # Advanced usage
//
// You can use a background thread to service connections provided:
// async_dispatcher_t for the background thread is stopped or suspended
// prior to destroying the class object.
//
// # Maintainer's Note
//
// This class' API is semantically identical to the one found in
// `//sdk/lib/sys/cpp`. This exists in order to offer equivalent facilities to
// the new C++ bindings. The other class is designed for the older, HLCPP
// (High-Level C++) FIDL bindings. It is expected that once
// all clients of HLCPP are migrated to this unified bindings, that library will
// be removed.
class OutgoingDirectory final {
 public:
  // Creates an OutgoingDirectory which will serve requests when
  // |Serve| or |ServeFromStartupInfo()| is called.
  //
  // |dispatcher| must not be nullptr. If it is, this method will panic.
  static OutgoingDirectory Create(async_dispatcher_t* dispatcher);

  OutgoingDirectory() = delete;

  // OutgoingDirectory can be moved. Once moved, invoking a method on an
  // instance will yield undefined behavior.
  OutgoingDirectory(OutgoingDirectory&&) noexcept;
  OutgoingDirectory& operator=(OutgoingDirectory&&) noexcept;

  // OutgoingDirectory cannot be copied.
  OutgoingDirectory(const OutgoingDirectory&) = delete;
  OutgoingDirectory& operator=(const OutgoingDirectory&) = delete;

  ~OutgoingDirectory();

  // Starts serving the outgoing directory on the given channel.
  //
  // This should be invoked after the outgoing directory has been populated, i.e. after
  // |AddProtocol|. While |OutgoingDirectory| does not require calling |AddProtocol|
  // before |Serve|, if you call them in the other order there is a chance that requests
  // that arrive in between will be dropped.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel. Note that this method returns immediately and that the |dispatcher|
  // provided to the constructor will be responsible for processing messages
  // sent to the server endpoint.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_server_end| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_server_end| has insufficient rights.
  zx::status<> Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_server_end);

  // Starts serving the outgoing directory on the channel provided to this
  // process at startup as |PA_DIRECTORY_REQUEST|.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // If |dispatcher| is nullptr, then the global dispatcher set via
  // |async_get_default_dispatcher| will be used.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: the process did not receive a |PA_DIRECTORY_REQUEST|
  // startup handle or it was already taken.
  //
  // ZX_ERR_ACCESS_DENIED: The |PA_DIRECTORY_REQUEST| handle has insufficient
  // rights.
  zx::status<> ServeFromStartupInfo();

  // Adds a FIDL Protocol instance.
  //
  // |impl| will be used to handle requests for this protocol.
  // |name| is used to determine where to host the protocol. This protocol will
  // be hosted under the path /svc/{name} where name is the discoverable name
  // of the protocol by default.
  //
  // Note, if and when |RemoveProtocol| is called for the provided |name|, this
  // object will asynchronously close down the associated server end channel and
  // stop receiving requests. This method provides no facilities for waiting
  // until teardown is complete. If such control is desired, then the
  // |TypedHandler| overload of this method listed below ought to be used.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry already exists for this protocol.
  //
  // ZX_ERR_INVALID_ARGS: |impl| is nullptr or |name| is an empty string.
  //
  // # Examples
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/cpp/outgoing_directory_test.cc
  template <typename Protocol>
  zx::status<> AddProtocol(fidl::WireServer<Protocol>* impl,
                           cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    static_assert(fidl::IsProtocol<Protocol>(), "Type of |Protocol| must be FIDL protocol");

    return AddProtocolAt(kServiceDirectoryWithNoSlash, impl, name);
  }

  // Same as above but overloaded to support servers implementations speaking
  // FIDL C++ natural types: |fidl::Server<P>|, part of the unified bindings.
  template <typename Protocol>
  zx::status<> AddProtocol(fidl::Server<Protocol>* impl,
                           cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    static_assert(fidl::IsProtocol<Protocol>(), "Type of |Protocol| must be FIDL protocol");
    if (impl == nullptr || dispatcher_ == nullptr) {
      return zx::make_status(ZX_ERR_INVALID_ARGS);
    }

    return AddProtocol<Protocol>(
        [dispatcher = dispatcher_, impl](fidl::ServerEnd<Protocol> request) {
          // This object is safe to drop. Server will still begin to operate
          // past its lifetime.
          auto _server = fidl::BindServer(dispatcher, std::move(request), impl);
        },
        name);
  }

  // Adds a FIDL Protocol instance.
  //
  // A |handler| is added to handle connection requests for the this particular
  // protocol. |name| is used to determine where to host the protocol.
  // This protocol will be hosted under the path /svc/{name} where name
  // is the discoverable name of the protocol.
  //
  // # Note
  //
  // Active connections are never torn down when/if |RemoveProtocol| is invoked
  // with the same |name|. Users of this method should manage teardown of
  // all active connections.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry already exists for this protocol.
  //
  // ZX_ERR_INVALID_ARGS: |name| is an empty string.
  //
  // # Examples
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/cpp/outgoing_directory_test.cc
  template <typename Protocol>
  zx::status<> AddProtocol(TypedHandler<Protocol> handler,
                           cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    static_assert(fidl::IsProtocol<Protocol>(), "Type of |Protocol| must be FIDL protocol");

    return AddProtocolAt<Protocol>(kServiceDirectoryWithNoSlash, std::move(handler), name);
  }

  // Same as above but is untyped. This method is generally discouraged but
  // is made available if a generic handler needs to be provided.
  zx::status<> AddProtocol(AnyHandler handler, cpp17::string_view name);

  // Same as above but allows overriding the parent directory in which the
  // protocol will be hosted.
  //
  // |path| is used as the parent directory for the protocol, e.g. `svc`.
  // |name| is the name under |path| at which the protocol will be hosted. By
  // default, the FIDL protocol name, e.g. `fuchsia.logger.LogSink`, is used.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry already exists for this protocol.
  //
  // ZX_ERR_INVALID_ARGS: |impl| is nullptr. |path| or |name| is an empty
  // string.
  template <typename Protocol>
  zx::status<> AddProtocolAt(cpp17::string_view path, fidl::WireServer<Protocol>* impl,
                             cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    static_assert(fidl::IsProtocol<Protocol>(), "Type of |Protocol| must be FIDL protocol");
    if (impl == nullptr || dispatcher_ == nullptr) {
      return zx::make_status(ZX_ERR_INVALID_ARGS);
    }

    return AddProtocolAt<Protocol>(
        path,
        [dispatcher = dispatcher_, impl,
         unbind_protocol_callbacks = unbind_protocol_callbacks_.get(),
         name = std::string(name)](fidl::ServerEnd<Protocol> request) {
          fidl::ServerBindingRef<Protocol> server =
              fidl::BindServer(dispatcher, std::move(request), impl);

          auto cb = [server = std::move(server)]() mutable { server.Unbind(); };
          // We don't have to check for entry existing because the |AddProtocol|
          // overload being invoked here will do that internally.
          AppendUnbindConnectionCallback(unbind_protocol_callbacks, name, std::move(cb));
        },
        name);
  }

  template <typename Protocol>
  zx::status<> AddProtocolAt(cpp17::string_view path, TypedHandler<Protocol> handler,
                             cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    static_assert(fidl::IsProtocol<Protocol>(), "Type of |Protocol| must be FIDL protocol");

    auto bridge_func = [handler = std::move(handler)](zx::channel request) {
      fidl::ServerEnd<Protocol> server_end(std::move(request));
      (void)handler(std::move(server_end));
    };

    return AddProtocolAt(std::move(bridge_func), path, name);
  }

  // Same as |AddProtocol| but is untyped and allows the usage of setting the
  // parent directory in which the protocol will be installed.
  zx::status<> AddProtocolAt(AnyHandler handler, cpp17::string_view path, cpp17::string_view name);

  // Adds an instance of a FIDL Service.
  //
  // A |handler| is added to provide an |instance| of a service.
  //
  // The template type |Service| must be the generated type representing a FIDL Service.
  // The generated class |Service::Handler| helps the caller populate a
  // |ServiceInstanceHandler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The instance already exists.
  //
  // ZX_ERR_INVALID_ARGS: |instance| is an empty string or |handler| is empty.
  //
  // # Example
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/cpp/outgoing_directory_test.cc
  template <typename Service>
  zx::status<> AddService(ServiceInstanceHandler handler,
                          cpp17::string_view instance = kDefaultInstance) {
    static_assert(fidl::IsService<Service>(), "Type of |Service| must be FIDL service");

    return AddService(std::move(handler), Service::Name, instance);
  }

  // Same as above but is untyped.
  zx::status<> AddService(ServiceInstanceHandler handler, cpp17::string_view service,
                          cpp17::string_view instance = kDefaultInstance);

  // Serve a subdirectory at the root of this outgoing directory.
  //
  // The directory will be installed under the path |directory_name|. When
  // a request is received under this path, then it will be forwarded to
  // |remote_dir|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry with the provided name already exists.
  //
  // ZX_ERR_BAD_HANDLE: |remote_dir| is an invalid handle.
  //
  // ZX_ERR_INVALID_ARGS: |directory_name| is an empty string.
  zx::status<> AddDirectory(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                            cpp17::string_view directory_name);

  // Same as |AddDirectory| but allows setting the parent directory
  // in which the directory will be installed.
  zx::status<> AddDirectoryAt(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                              cpp17::string_view path, cpp17::string_view directory_name);

  // Removes a FIDL Protocol entry with the path `/svc/{name}`.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The protocol entry was not found.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveProtocol<lib_example::MyProtocol>();
  // ```
  template <typename Protocol>
  zx::status<> RemoveProtocol(cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    return RemoveProtocol(name);
  }

  // Same as above but untyped.
  zx::status<> RemoveProtocol(cpp17::string_view name);

  // Removes a FIDL Protocol entry located in the provided |directory|.
  // Unlike |RemoveProtocol| which looks for the protocol to remove in the
  // path `/svc/{name}`, this method uses the directory name provided, e.g.
  // `/{path}/{name}`.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The protocol entry was not found.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveProtocolAt<lib_example::MyProtocol>("diagnostics");
  // ```
  template <typename Protocol>
  zx::status<> RemoveProtocolAt(
      cpp17::string_view path, cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    return RemoveProtocolAt(path, name);
  }

  // Same as above but untyped.
  zx::status<> RemoveProtocolAt(cpp17::string_view directory, cpp17::string_view name);

  // Removes an instance of a FIDL Service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveService<lib_example::MyService>("my-instance");
  // ```
  template <typename Service>
  zx::status<> RemoveService(cpp17::string_view instance = kDefaultInstance) {
    return RemoveService(Service::Name, instance);
  }

  // Same as above but untyped.
  zx::status<> RemoveService(cpp17::string_view service,
                             cpp17::string_view instance = kDefaultInstance);

  // Removes the subdirectory on the provided |directory_name|.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: No entry was found with provided name.
  zx::status<> RemoveDirectory(cpp17::string_view directory_name);

  // Same as |RemoveDirectory| but allows specifying the parent directory
  // that the directory will be removed from. The parent directory, |path|,
  // will not be removed.
  zx::status<> RemoveDirectoryAt(cpp17::string_view path, cpp17::string_view directory_name);

 private:
  OutgoingDirectory(async_dispatcher_t* dispatcher, svc_dir_t* root);

  // |svc_dir_add_service_by_path| takes in a void* |context| that is passed to
  // the |handler| callback passed as the last argument to the function call.
  // This library will pass in a casted void* pointer to this object, and when
  // the `svc` library invokes this library's connection handler, the |context|
  // will be casted back to |OnConnectContext*|.
  struct OnConnectContext {
    AnyHandler handler;
  };

  // Function pointer that matches type of |svc_dir_add_service| handler.
  // Internally, it calls the |AnyMemberHandler| instance populated via |AddAnyMember|.
  static void OnConnect(void* context, const char* service_name, zx_handle_t handle);

  // Callback invoked during teardown of a FIDL protocol entry. This callback
  // will close all active connections on the associated channel.
  using UnbindConnectionCallback = fit::callback<void()>;
  using UnbindCallbackMap = std::map<std::string, std::vector<UnbindConnectionCallback>>;

  static void AppendUnbindConnectionCallback(UnbindCallbackMap* unbind_protocol_callbacks,
                                             const std::string& name,
                                             UnbindConnectionCallback callback);

  void UnbindAllConnections(cpp17::string_view name);

  static std::string MakePath(cpp17::string_view service, cpp17::string_view instance);

  async_dispatcher_t* dispatcher_ = nullptr;

  svc_dir_t* root_ = nullptr;

  // Mapping of all registered protocol handlers. Key represents a path to
  // the directory in which the protocol ought to be installed. For example,
  // a path may look like `svc/fuchsia.FooService/some_instance`.
  // The value contains a map of each of the entry's handlers.
  //
  // For FIDL Protocols, entries will be stored under "svc" entry
  // of this type, and then their name will be used as a key for the internal
  // map.
  //
  // For FIDL Services, entries will be stored by instance,
  // e.g. `svc/fuchsia.FooService/default`, and then the member names will be
  // used as the keys for the internal maps.
  //
  // The OnConnectContext has to be stored in the heap because its pointer
  // is used by |OnConnect|, a static function, during channel connection attempt.
  std::map<std::string, std::map<std::string, std::unique_ptr<OnConnectContext>>>
      registered_handlers_ = {};

  // Protocol bindings used to initiate teardown when protocol is removed. We
  // store this in a callback as opposed to a map of fidl::ServerBindingRef<T>
  // because that object is template parameterized and therefore can't be
  // stored in a homogeneous container.
  //
  // Wrapped in unique_ptr so that we can capture in a lambda without risk of
  // it becoming invalid.
  std::unique_ptr<UnbindCallbackMap> unbind_protocol_callbacks_;
};

}  // namespace component

#endif  // LIB_SYS_COMPONENT_CPP_OUTGOING_DIRECTORY_H_
