// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers for using |ServiceProvider|s.

#ifndef APPS_MODULAR_LIB_APP_CONNECT_H_
#define APPS_MODULAR_LIB_APP_CONNECT_H_

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "apps/modular/services/application/service_provider.fidl.h"

namespace modular {

// Helper for using a |ServiceProvider|'s |ConnectToService()| that takes a
// fully-typed interface request (and can use the default interface name). Note
// that this can be used in conjunction with |GetProxy()|, etc. E.g.:
//
//   FooPtr foo;
//   modular::ConnectToService(service_provider, fidl::GetProxy(&foo));
template <typename Interface>
inline void ConnectToService(
    ServiceProvider* service_provider,
    fidl::InterfaceRequest<Interface> interface_request,
    const std::string& interface_name = Interface::Name_) {
  service_provider->ConnectToService(interface_name,
                                     interface_request.PassMessagePipe());
}

}  // namespace modular

#endif  // APPS_MODULAR_LIB_APP_CONNECT_H_
