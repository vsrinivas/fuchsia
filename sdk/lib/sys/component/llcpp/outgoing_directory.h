// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_
#define LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/string_view.h>
#include <lib/svc/dir.h>
#include <lib/sys/component/llcpp/constants.h>
#include <lib/sys/component/llcpp/service_handler.h>
#include <zircon/assert.h>

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
  // Creates an OutgoingDirectory which will serve requests on |dispatcher|
  // when |Serve(zx::channel)| or |ServeFromStartupInfo()| is called.
  //
  // |dispatcher| must not be null.
  explicit OutgoingDirectory(async_dispatcher_t* dispatcher);

  ~OutgoingDirectory();

  // Outgoing objects cannot be copied.
  OutgoingDirectory(const OutgoingDirectory&) = delete;
  OutgoingDirectory& operator=(const OutgoingDirectory&) = delete;

  // Starts serving the outgoing directory on the given channel.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // This object will serve the outgoing directory using the |async_dispatcher_t|
  // provided in the constructor.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_request| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // ZX_ERR_ALREADY_EXISTS: |Serve| was already invoked previously.
  zx::status<> Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_request);

  // Starts serving the outgoing directory on the channel provided to this
  // process at startup as |PA_DIRECTORY_REQUEST|.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel.
  //
  // This object will serve the outgoing directory using the |async_dispatcher_t|
  // provided in the constructor.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: the process did not receive a |PA_DIRECTORY_REQUEST|
  // startup handle or it was already taken.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_request| has insufficient rights.
  //
  // ZX_ERR_ALREADY_EXISTS: |Serve| was already invoked previously.
  zx::status<> ServeFromStartupInfo();

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
  // ZX_ERR_ACCESS_DENIED: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // ```
  // sys::component::ServiceHandler handler;
  // ::lib_example::MyService::Handler my_handler(&handler);
  // my_handler.add_my_member([dispatcher](fidl::ServerEnd<FooProtocol> server_end) {
  //   fidl::BindServer(dispatcher, std::move(server_end), std::make_unique<FooProtocolImpl>());
  // });
  // outgoing.AddService<::lib_example::MyService>(std::move(handler), "my-instance");
  // ```
  template <typename Service>
  zx::status<> AddService(ServiceHandler handler, cpp17::string_view instance = kDefaultInstance) {
    return AddNamedService(std::move(handler), Service::Name, std::move(instance));
  }

  // Adds an instance of a service.
  //
  // A |handler| is added to provide an |instance| of a |service|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The instance already exists.
  //
  // ZX_ERR_ACCESS_DENIED: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // ```
  // sys::component::ServiceHandler handler;
  // handler.AddMember("my-member", ...);
  // outgoing.AddNamedService(std::move(handler), "lib.example.MyService", "my-instance");
  // ```
  zx::status<> AddNamedService(ServiceHandler handler, cpp17::string_view service,
                               cpp17::string_view instance = kDefaultInstance);

  // Removes an instance of a service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // ZX_ERR_ACCESS_DENIED: This instance is not serving the outgoing directory.
  // This is only returned if |Serve| or |ServeFromStartupInfo| is not called
  // before invoking this method.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveService<::lib_example::MyService>("my-instance");
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
  // ZX_ERR_ACCESS_DENIED: This instance is not serving the outgoing directory.
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
    ServiceHandler::Connector connector;
    OutgoingDirectory* self;
  };

  // Function pointer that matches type of |svc_dir_add_service| handler.
  // Internally, it calls the |AnyMemberHandler| instance populated via |AddAnyMember|.
  static void OnConnect(void* context, const char* service_name, zx_handle_t handle);

  static std::string MakePath(cpp17::string_view service, cpp17::string_view instance);

  async_dispatcher_t* dispatcher_ = nullptr;
  std::shared_ptr<OutgoingDirectory> this_ref_ = nullptr;

  svc_dir_t* root_ = nullptr;

  // Mapping of all registered service handlers. Key represents a path to
  // the directory in which the service ought to be installed. For example,
  // a path may look like "svc/fuchsia.FooService/some_instance".
  // The value contains a stack of each of the instance's member handler.
  // The OnConnectContext has to be stored in the heap because its pointer
  // is used by |OnConnect|, a static function, during channel connection attempt.
  std::map<std::string, std::stack<OnConnectContext>> registered_service_connectors_ = {};
};

}  // namespace component_llcpp

#endif  // LIB_SYS_COMPONENT_LLCPP_OUTGOING_DIRECTORY_H_
