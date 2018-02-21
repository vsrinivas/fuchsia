// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_CONNECT_H_
#define GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_CONNECT_H_

#include <fdio/util.h>

#include "lib/fidl/cpp/bindings/interface_ptr.h"

extern std::string svc_prefix;

template <typename T>
zx_status_t connect(fidl::InterfacePtr<T>* ptr) {
  std::string svc_path = svc_prefix + T::Name_;
  zx_status_t status = fdio_service_connect(
      svc_path.c_str(), ptr->NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    std::cerr << "Failed to connect to " << svc_path << "\n";
    return ZX_ERR_UNAVAILABLE;
  }
  return ZX_OK;
}

#endif  // GARNET_BIN_GUEST_TOOLS_INSPECT_GUEST_CONNECT_H_
