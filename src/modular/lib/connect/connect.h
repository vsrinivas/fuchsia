// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_CONNECT_CONNECT_H_
#define SRC_MODULAR_LIB_CONNECT_CONNECT_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "lib/fidl/cpp/interface_request.h"

namespace connect {

// Helper for using a |ServiceProvider|'s |ConnectToService()| that takes a
// fully-typed interface request (and can use the default interface name).
template <typename Interface>
inline void ConnectToService(fuchsia::sys::ServiceProvider* service_provider,
                             fidl::InterfaceRequest<Interface> interface_request,
                             const std::string& interface_name = Interface::Name_) {
  service_provider->ConnectToService(interface_name, interface_request.TakeChannel());
}

}  // namespace connect

#endif  // SRC_MODULAR_LIB_CONNECT_CONNECT_H_
