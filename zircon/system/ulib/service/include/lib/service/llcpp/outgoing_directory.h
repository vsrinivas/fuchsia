// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SERVICE_LLCPP_OUTGOING_DIRECTORY_H_
#define LIB_SERVICE_LLCPP_OUTGOING_DIRECTORY_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/string_view.h>
#include <lib/service/llcpp/constants.h>
#include <lib/service/llcpp/service_handler.h>

#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>

namespace llcpp::sys {

// The directory provided by this component to the component manager.
//
// A component's outgoing directory contains services, data, and other objects
// that can be consumed by either the component manager itself or by other
// components in the system.
//
// The outgoing directory contains several subdirectories with well-known
// names:
//
//  * svc. This directory contains the services offered by this component to
//    other components.
//  * debug. This directory contains arbitrary debugging output offered by this
//    component.
//
// The outgoing directory may optionally contain other directories constructed
// using |GetOrCreateDirectory|. Common optional directories include:
//
//  * objects. This directory contains Inspect API files and interfaces for use
//    in component inspection.
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
// This class is mostly copied from `//sdk/lib/sys/cpp`.
//
// This library relies on the LLCPP (Low-level C++) FIDL library in `//zircon`
// and is not yet in the SDK. Code in `//zircon` cannot depend on libraries in
// the SDK.
//
// The main difference between this class and the one in the SDK is that
// this one uses LLCPP interfaces instead of HLCPP. The underlying
// VFS implementations differ but can be merged at a future date.
class OutgoingDirectory final {
 public:
  // Creates an OutgoingDirectory which will serve requests on |dispatcher|
  // when |Serve(zx::channel)| or |ServeFromStartupInfo()| is called.
  //
  // |dispatcher| must not be null. See |async_get_default_dispatcher()|.
  OutgoingDirectory(async_dispatcher_t* dispatcher);

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
  zx_status_t Serve(::zx::channel directory_request);

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
  zx_status_t ServeFromStartupInfo();

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
  // # Example
  //
  // ```
  // llcpp::sys::ServiceHandler handler;
  // ::llcpp::lib::example::MyService::Handler my_handler(&handler);
  // my_handler.add_my_member([dispatcher](zx::channel request) {
  //   fidl::Bind(dispatcher, std::move(request), std::make_unique<FooProtocolImpl>());
  // });
  // outgoing.AddService<::llcpp::lib::example::MyService>(std::move(handler), "my-instance");
  // ```
  template <typename Service>
  zx_status_t AddService(ServiceHandler handler,
                         fit::string_view instance = kDefaultInstance) const {
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
  // # Example
  //
  // ```
  // llcpp::sys::ServiceHandler handler;
  // handler.AddMember("my-member", ...);
  // outgoing.AddNamedService(std::move(handler), "lib.example.MyService", "my-instance");
  // ```
  zx_status_t AddNamedService(ServiceHandler handler, fit::string_view service,
                              fit::string_view instance = kDefaultInstance) const;

  // Removes an instance of a service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveService<::llcpp::lib::example::MyService>("my-instance");
  // ```
  template <typename Service>
  zx_status_t RemoveService(fit::string_view instance) const {
    return RemoveNamedService(Service::Name, instance);
  }

  // Removes an instance of a service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  zx_status_t RemoveNamedService(fit::string_view service, fit::string_view instance) const;

  // Gets the root directory.
  fbl::RefPtr<fs::PseudoDir> root_dir() const { return root_; }

  // Gets the directory to publish debug data.
  fbl::RefPtr<fs::PseudoDir> debug_dir() const { return debug_; }

 private:
  // Serves the virtual filesystem.
  fs::SynchronousVfs vfs_;

  // The root of the outgoing directory itself.
  fbl::RefPtr<fs::PseudoDir> root_;

  // The service subdirectory of the root directory.
  fbl::RefPtr<fs::PseudoDir> svc_;

  // The debug subdirectory of the root directory.
  fbl::RefPtr<fs::PseudoDir> debug_;
};

}  // namespace llcpp::sys

#endif  // LIB_SERVICE_LLCPP_OUTGOING_DIRECTORY_H_
