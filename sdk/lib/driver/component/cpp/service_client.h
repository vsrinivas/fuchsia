// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_SERVICE_CLIENT_H_
#define LIB_DRIVER_COMPONENT_CPP_SERVICE_CLIENT_H_

#include <lib/driver/component/cpp/namespace.h>

namespace driver {

// Connects to the |ServiceMember| protocol in the namespace |ns|.
//
// |instance| refers to the name of the instance of the service.
//
// Returns a ClientEnd of type corresponding to the given protocol
// e.g. fidl::ClientEnd or fdf::ClientEnd.
template <typename ServiceMember,
          typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
zx::result<fidl::internal::ClientEndType<typename ServiceMember::ProtocolType>> Connect(
    const driver::Namespace& ns, std::string_view instance = component::kDefaultInstance) {
  return ns.Connect<ServiceMember>(instance);
}

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_SERVICE_CLIENT_H_
