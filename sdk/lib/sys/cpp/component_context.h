// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_COMPONENT_CONTEXT_H_
#define LIB_SYS_CPP_COMPONENT_CONTEXT_H_

#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

namespace sys {

// Context information that this component received at startup.
//
// Upon creation, components are given a namespace, which is file system local
// to the component. A components namespace lets the component interact with
// other components and the system at large. One important part of this
// namespace is the directory of services, typically located at "/svc" in the
// components namespace. The |ComponentContext| provides an ergonomic interface
// to this service bundle through its |svc()| property.
//
// In addition to receiving services, components can also publish services and
// data to other components through their outgoing namespace, which is also a
// directory. The |ComponentContext| provides an ergonomic interface for
// exposing services and other file system objects through its |outgoing()|
// property.
//
// This class is thread-hostile.
//
//  # Simple usage
//
// Instances of this class should be owned and managed on the same thread.
//
// # Advanced usage
//
// You can use a background thread to service this class provided:
// async_dispatcher_t for the background thread is stopped or suspended
// prior to destroying the class object.
//
// # Example
//
// The |ComponentContext| object is typically created early in the startup
// sequence for components, typically after creating the |async::Loop| for the
// main thread.
//
// ```
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
//   auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
//   my::App app(std::move(context))
//   loop.Run();
//   return 0;
// }
// ```
class ComponentContext final {
 public:
  // Creates a component context that uses |svc| for incoming services. Callers
  // can call |OutgoingDirectory::Serve()| if they wish to publish the outgoing
  // directory to a channel they provide.
  //
  // This constructor is rarely used directly. Instead, most clients create a
  // component context using the |Create()| static method.
  explicit ComponentContext(std::shared_ptr<ServiceDirectory> svc,
                            async_dispatcher_t* dispatcher = nullptr);

  // Creates a component context that uses |svc| for incoming services and immediately
  // binds the local outgoing directory implementation to |outgoing_directory_request|. Callers
  // SHOULD make all modifications to |outgoing()| before |dispatcher| starts processing incoming
  // requests. Exceptions to this guideline are rare.
  ComponentContext(std::shared_ptr<ServiceDirectory> svc, zx::channel outgoing_directory_request,
                   async_dispatcher_t* dispatcher = nullptr);

  ~ComponentContext();

  // ComponentContext objects cannot be copied.
  ComponentContext(const ComponentContext&) = delete;
  ComponentContext& operator=(const ComponentContext&) = delete;

  // Creates a component context from the process startup info without binding a channel
  // handle to the outgoing directory. Clients wishing to serve the outgoing directory MUST
  // call either |outgoing()->ServeFromStartupInfo()| to serve to the PA_DIRECTORY_REQUEST
  // handle.
  //
  // Callers SHOULD make all modifications to |outgoing()| before calling one of the above
  // Serve methods. Exceptions to this guideline are rare.
  //
  // Call this function once during process initialization to retrieve the
  // handles supplied to the component by the component manager. This function
  // consumes some of those handles, which means subsequent calls to this
  // function will not return a functional component context.
  //
  // Prefer creating the |ComponentContext| in the |main| function for a
  // component and passing the context to a class named "App" which encapsulates
  // the main logic of the program. This pattern makes testing easier because
  // tests can pass a fake |ComponentContext| from |ComponentContextProvider| to
  // the |App| class to inject dependencies.
  //
  // The returned unique_ptr is never null.
  //
  // # Example
  //
  // ```
  // int main(int argc, const char** argv) {
  //   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  //   auto context = sys::ComponentContext::Create();
  //   my::App app(context.get())
  //   app.PerformAsyncInitialization(/*when_init_done=*/[context = context.get()] {
  //     // Delaying serving of the out/ directory to here ensures no race condition
  //     // exists between clients looking for entries in out/ before app
  //     // initialization has had a chance to create them.
  //     context->outgoing()->ServeFromStartupInfo();
  //   });
  //   loop.Run();
  //   return 0;
  // }
  // ```
  static std::unique_ptr<ComponentContext> Create();

  // Equivalent to |Create()| followed immediately by |outgoing()->ServeFromStartupInfo()|.
  //
  // Callers SHOULD make all modifications to |outgoing()| before the thread default
  // async_dispatcher_t starts processing incoming requests. Exceptions to this guideline are rare.
  //
  // Call this function once during process initialization to retrieve the
  // handles supplied to the component by the component manager. This function
  // consumes some of those handles, which means subsequent calls to this
  // function will not return a functional component context.
  //
  // Prefer creating the |ComponentContext| in the |main| function for a
  // component and passing the context to a class named "App" which encapsulates
  // the main logic of the program. This pattern makes testing easier because
  // tests can pass a fake |ComponentContext| from |ComponentContextProvider| to
  // the |App| class to inject dependencies.
  //
  // The returned unique_ptr is never null.
  //
  // # Example
  //
  // ```
  // int main(int argc, const char** argv) {
  //   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  //   auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  //   my::App app(std::move(context))
  //   loop.Run();
  //   return 0;
  // }
  // ```
  static std::unique_ptr<ComponentContext> CreateAndServeOutgoingDirectory();

  // The component's incoming directory of services from its namespace.
  //
  // Use this object to connect to services offered by other components.
  //
  // The returned object is thread-safe.
  //
  // # Example
  //
  // ```
  // auto controller = context.svc()->Connect<fuchsia::foo::Controller>();
  // ```
  const std::shared_ptr<ServiceDirectory>& svc() const { return svc_; }

  // The component's outgoing directory.
  //
  // Use this object to publish services and data to the component manager and
  // other components.
  //
  // The returned object is thread-safe.
  //
  // # Example
  //
  // ```
  // class App : public fuchsia::foo::Controller {
  //  public:
  //   App(std::unique_ptr<ComponentContext> context)
  //     : context_(std::move(context) {
  //     context_.outgoing()->AddPublicService(bindings_.GetHandler(this));
  //   }
  //
  //   // fuchsia::foo::Controller implementation:
  //   [...]
  //
  //  private:
  //   fidl::BindingSet<fuchsia::foo::Controller> bindings_;
  // }
  // ```
  const std::shared_ptr<OutgoingDirectory>& outgoing() const { return outgoing_; }
  std::shared_ptr<OutgoingDirectory>& outgoing() { return outgoing_; }

 private:
  std::shared_ptr<ServiceDirectory> svc_;
  std::shared_ptr<OutgoingDirectory> outgoing_;
  zx::channel outgoing_directory_request;
};

}  // namespace sys

#endif  // LIB_SYS_CPP_COMPONENT_CONTEXT_H_
