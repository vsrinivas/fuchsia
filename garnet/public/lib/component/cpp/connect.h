// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers for using |ServiceProvider|s.
//
// The functions in this header deprecated along with |ServiceProvider| itself.
// Please use the functions in lib/svc/cpp/services.h instead.

#ifndef LIB_APP_CPP_CONNECT_H_
#define LIB_APP_CPP_CONNECT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fidl/cpp/interface_request.h"

namespace component {

// Helper for using a |ServiceProvider|'s |ConnectToService()| that creates
// a new channel and returns a fully-typed interface pointer (and can use
// the default interface name).
template <typename Interface>
inline fidl::InterfacePtr<Interface> ConnectToService(
    fuchsia::sys::ServiceProvider* service_provider,
    const std::string& interface_name = Interface::Name_) {
  fidl::InterfacePtr<Interface> interface_ptr;
  service_provider->ConnectToService(interface_name,
                                     interface_ptr.NewRequest().TakeChannel());
  return interface_ptr;
}

// Helper for using a |ServiceProvider|'s |ConnectToService()| that takes a
// fully-typed interface request (and can use the default interface name).
template <typename Interface>
inline void ConnectToService(
    fuchsia::sys::ServiceProvider* service_provider,
    fidl::InterfaceRequest<Interface> interface_request,
    const std::string& interface_name = Interface::Name_) {
  service_provider->ConnectToService(interface_name,
                                     interface_request.TakeChannel());
}

}  // namespace component

#endif  // LIB_APP_CPP_CONNECT_H_
