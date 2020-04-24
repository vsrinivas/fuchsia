// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app.h"

const char* kSysmemClassPath = "/dev/class/sysmem";

App::App() : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {
  zx_status_t status = sysmem_connector_init(kSysmemClassPath, &sysmem_connector_);
  if (status != ZX_OK) {
    printf(
        "sysmem_connector sysmem_connector_init() failed - exiting - status: "
        "%d\n",
        status);
    // Without sysmem_connector_init() success, we can't serve anything, so
    // exit.
    exit(-1);
  }

  component_context_->outgoing()->AddPublicService<fuchsia::sysmem::Allocator>(
      [this](fidl::InterfaceRequest<fuchsia::sysmem::Allocator> request) {
        // Normally a service would directly serve the server end of the
        // channel, but in this case we forward the service request to the
        // sysmem driver.  We do the forwarding via code we share with a similar
        // Zircon service.
        sysmem_connector_queue_connection_request(sysmem_connector_,
                                                  request.TakeChannel().release());
      });
}

App::~App() {
  sysmem_connector_release(sysmem_connector_);
  sysmem_connector_ = nullptr;
}
