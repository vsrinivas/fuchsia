// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT2_CPP_STARTUP_CONTEXT_H_
#define LIB_COMPONENT2_CPP_STARTUP_CONTEXT_H_

#include <memory>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component2/cpp/outgoing.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

namespace component2 {

// Provides access to the component's startup context and allows the component
// to publish services back to its creator.
class StartupContext {
 public:
  // The constructor is normally called by CreateFromStartupInfo().
  StartupContext(zx::channel service_root, zx::channel directory_request,
                 async_dispatcher_t* dispatcher = nullptr);

  virtual ~StartupContext();

  StartupContext(const StartupContext&) = delete;
  StartupContext& operator=(const StartupContext&) = delete;

  // Creates the component context from the process startup info.
  //
  // This function should be called once during process initialization to
  // retrieve the handles supplied to the component by the component
  // manager.
  //
  // The returned unique_ptr is never null.
  static std::unique_ptr<StartupContext> CreateFromStartupInfo();

  static std::unique_ptr<StartupContext> CreateFrom(
      fuchsia::sys::StartupInfo startup_info);

  const Outgoing& outgoing() const { return outgoing_; }

  // Connects to a service provided by the component's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> Connect(
      const std::string& interface_name = Interface::Name_) {
    fidl::InterfacePtr<Interface> result;
    Connect(result.NewRequest(), interface_name);
    return std::move(result);
  }

  // Connects to a service provided by the component's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void Connect(fidl::InterfaceRequest<Interface> request,
               const std::string& interface_name = Interface::Name_) {
    Connect(interface_name, request.TakeChannel());
  }

  // Connects to a service provided by the component's environment,
  // binding the service to a channel.
  void Connect(const std::string& interface_name, zx::channel channel);

 private:
  zx::channel service_root_;
  Outgoing outgoing_;
};

}  // namespace component2

#endif  // LIB_COMPONENT2_CPP_STARTUP_CONTEXT_H_
