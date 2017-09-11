// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers for using |ServiceProvider|s.

#ifndef APPLICATION_LIB_APP_CONNECT_H_
#define APPLICATION_LIB_APP_CONNECT_H_

#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace app {

// Helper for using a |ServiceProvider|'s |ConnectToService()| that creates
// a new channel and returns a fully-typed interface pointer (and can use
// the default interface name).
template <typename Interface>
inline fidl::InterfacePtr<Interface> ConnectToService(
    ServiceProvider* service_provider,
    const std::string& interface_name = Interface::Name_) {
  fidl::InterfacePtr<Interface> interface_ptr;
  service_provider->ConnectToService(interface_name,
                                     interface_ptr.NewRequest().PassChannel());
  return interface_ptr;
}

// Helper for using a |ServiceProvider|'s |ConnectToService()| that takes a
// fully-typed interface request (and can use the default interface name).
template <typename Interface>
inline void ConnectToService(
    ServiceProvider* service_provider,
    fidl::InterfaceRequest<Interface> interface_request,
    const std::string& interface_name = Interface::Name_) {
  service_provider->ConnectToService(interface_name,
                                     interface_request.PassChannel());
}

}  // namespace app

#endif  // APPLICATION_LIB_APP_CONNECT_H_
