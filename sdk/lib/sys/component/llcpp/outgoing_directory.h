// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_
#define LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/string_view.h>
#include <lib/svc/dir.h>
#include <lib/sys/component/llcpp/constants.h>
#include <lib/sys/component/llcpp/handlers.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <memory>
#include <stack>

namespace component_llcpp {

// The directory containing handles to capabilities this component provides.
// Entries served from this outgoing directory should correspond to the
// component manifest's `capabilities` declarations.
//
// The outgoing directory contains one special subdirectory, named `svc`. This
// directory contains the FIDL Services and Protocols offered by this component
// to other components. For example the FIDL Protocol `fuchsia.foo.Bar` will be
// hosted under the path `/svc/fuchsia.foo.Bar`.
//
// This class is thread-hostile.
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
// LLCPP (Low-level C++) FIDL bindings support. The other class is designed for
// HLCPP( High-Level C++) FIDL bindings.
class OutgoingDirectory final {
 public:
  // Creates an OutgoingDirectory which will serve requests when
  // |Serve(zx::channel)| or |ServeFromStartupInfo()| is called.
  OutgoingDirectory() = default;

  ~OutgoingDirectory();

  // Outgoing objects cannot be copied.
  OutgoingDirectory(const OutgoingDirectory&) = delete;
  OutgoingDirectory& operator=(const OutgoingDirectory&) = delete;

  // Starts serving the outgoing directory on the given channel.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // If |dispatcher| is nullptr, then the global dispatcher set via
  // |async_get_default_dispatcher| will be used.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_request| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // ZX_ERR_ALREADY_EXISTS: |Serve| was already invoked previously.
  //
  // ZX_ERR_INVALID_ARGS: |dispatcher| is nullptr and no dispatcher is set
  // globally via |async_set_default_dispatcher|.
  zx::status<> Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_request,
                     async_dispatcher_t* dispatcher = nullptr);

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
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // ZX_ERR_ALREADY_EXISTS: |Serve| was already invoked previously.
  zx::status<> ServeFromStartupInfo(async_dispatcher_t* dispatcher = nullptr);

  // Adds a protocol instance.
  //
  // |impl| will be used to handle requests for this protocol.
  // |name| is used to determine where to host the protocol. This protocol will
  // be hosted under the path /svc/{name} where name is the discoverable name
  // of the protocol.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry already exists for this protocol.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // ZX_ERR_INVALID_ARGS: |server| or |dispatcher| is nullptr. |dispatcher|
  // may be nullptr iff it is configured using |async_set_default_dispatcher|.
  //
  // # Examples
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/llcpp/outgoing_directory_test.cc
  template <typename Protocol>
  zx::status<> AddProtocol(fidl::WireServer<Protocol>* impl,
                           async_dispatcher_t* dispatcher = nullptr,
                           cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    if (dispatcher == nullptr) {
      dispatcher = async_get_default_dispatcher();
    }

    if (impl == nullptr || dispatcher == nullptr) {
      return zx::make_status(ZX_ERR_INVALID_ARGS);
    }

    return AddProtocol<Protocol>(
        [=](fidl::ServerEnd<Protocol> request) {
          // This object is safe to drop. Server will still begin to operate
          // past its lifetime.
          auto _server = fidl::BindServer(dispatcher, std::move(request), impl);
        },
        name);
  }

  // Adds a protocol instance.
  //
  // A |handler| is added to handle connection requests for the this particular
  // protocol. |name| is used to determine where to host the protocol.
  // This protocol will be hosted under the path /svc/{name} where name
  // is the discoverable name of the protocol.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry already exists for this protocol.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Examples
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/llcpp/outgoing_directory_test.cc
  template <typename Protocol>
  zx::status<> AddProtocol(TypedHandler<Protocol> handler,
                           cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    auto bridge_func = [handler = std::move(handler)](zx::channel request) {
      fidl::ServerEnd<Protocol> server_end(std::move(request));
      (void)handler(std::move(server_end));
    };

    return AddNamedProtocol(std::move(bridge_func), name);
  }

  // Same as above but is untyped. This method is generally discouraged but
  // is made available if a generic handler needs to be provided.
  zx::status<> AddNamedProtocol(AnyHandler handler, cpp17::string_view name);

  // Adds an instance of a service.
  //
  // A |handler| is added to provide an |instance| of a service.
  //
  // The template type |Service| must be the generated type representing a FIDL Service.
  // The generated class |Service::Handler| helps the caller populate a |ServiceHandler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The instance already exists.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/llcpp/outgoing_directory_test.cc
  template <typename Service>
  zx::status<> AddService(ServiceHandler handler, cpp17::string_view instance = kDefaultInstance) {
    return AddNamedService(std::move(handler), Service::Name, instance);
  }

  // Adds an instance of a service.
  //
  // A |handler| is added to provide an |instance| of a |service|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The instance already exists.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // See sample use cases in test case(s) located at
  // //sdk/lib/sys/component/llcpp/outgoing_directory_test.cc
  zx::status<> AddNamedService(ServiceHandler handler, cpp17::string_view service,
                               cpp17::string_view instance = kDefaultInstance);

  // Removes a protocol entry.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The protocol entry was not found.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveProtocol<lib_example::MyProtocol>();
  // ```
  template <typename Protocol>
  zx::status<> RemoveProtocol(cpp17::string_view name = fidl::DiscoverableProtocolName<Protocol>) {
    return RemoveNamedProtocol(name);
  }

  // Same as above but untyped.
  zx::status<> RemoveNamedProtocol(cpp17::string_view name);

  // Removes an instance of a service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveService<lib_example::MyService>("my-instance");
  // ```
  template <typename Service>
  zx::status<> RemoveService(cpp17::string_view instance = kDefaultInstance) {
    return RemoveNamedService(Service::Name, instance);
  }

  // Removes an instance of a service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // ZX_ERR_BAD_HANDLE: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  zx::status<> RemoveNamedService(cpp17::string_view service,
                                  cpp17::string_view instance = kDefaultInstance);

 private:
  // |svc_dir_add_service_by_path| takes in a void* |context| that is passed to
  // the |handler| callback passed as the last argument to the function call.
  // This library will pass in a casted void* pointer to this object, and when
  // the `svc` library invokes this library's connection handler, the |context|
  // will be casted back to |OnConnectContext*|.
  struct OnConnectContext {
    AnyHandler handler;
    OutgoingDirectory* self;
  };

  // Function pointer that matches type of |svc_dir_add_service| handler.
  // Internally, it calls the |AnyMemberHandler| instance populated via |AddAnyMember|.
  static void OnConnect(void* context, const char* service_name, zx_handle_t handle);

  static std::string MakePath(cpp17::string_view service, cpp17::string_view instance);

  svc_dir_t* root_ = nullptr;

  // Mapping of all registered protocol handlers. Key represents a path to
  // the directory in which the protocol ought to be installed. For example,
  // a path may look like "svc/fuchsia.FooService/some_instance".
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
  std::map<std::string, std::map<std::string, OnConnectContext>> registered_handlers_ = {};
};

}  // namespace component_llcpp

#endif  // LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_
