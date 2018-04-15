// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_SERVICE_H_
#define GARNET_BIN_GUEST_CLI_SERVICE_H_

#include <fbl/function.h>
#include <fdio/util.h>
#include <iostream>

#include <fuchsia/cpp/machina.h>

// Interface of the inspect service of the guest.
extern machina::InspectServicePtr inspect_svc;
// Path to the inspect service of the guest.
extern std::string svc_path;

using InspectReq = fidl::InterfaceRequest<machina::InspectService>;
using ConnectFunc = fbl::Function<zx_status_t(InspectReq)>;

static inline zx_status_t connect(InspectReq req) {
  zx_status_t status =
      fdio_service_connect(svc_path.c_str(), req.TakeChannel().release());
  if (status != ZX_OK) {
    std::cerr << "Failed to connect to " << svc_path << "\n";
    return ZX_ERR_UNAVAILABLE;
  }
  return ZX_OK;
}

#endif  // GARNET_BIN_GUEST_CLI_SERVICE_H_
