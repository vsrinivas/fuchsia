// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysmem.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/sysmem-connector/sysmem-connector.h>
#include <lib/zx/channel.h>

#include <cstring>

const char* kSysmemSvchostPath = "/dev/class/sysmem";

// We don't really need a service context, only a sysmem-connector context, so
// we just directly use the sysmem-connector context as the only context.
static zx_status_t sysmem2_init(void** out_ctx) {
  return sysmem_connector_init(kSysmemSvchostPath, reinterpret_cast<sysmem_connector_t**>(out_ctx));
}

static zx_status_t sysmem2_connect(void* ctx, async_dispatcher_t* dispatcher,
                                   const char* service_name, zx_handle_t allocator_request_param) {
  zx::channel allocator_request(allocator_request_param);
  sysmem_connector_t* connector = static_cast<sysmem_connector_t*>(ctx);
  if (!strcmp(service_name, fuchsia_sysmem_Allocator_Name)) {
    sysmem_connector_queue_connection_request(connector, allocator_request.release());
  }
  return ZX_ERR_NOT_SUPPORTED;
}
static void sysmem2_release(void* ctx) {
  sysmem_connector_release(static_cast<sysmem_connector_t*>(ctx));
}

static constexpr const char* sysmem2_services[] = {
    fuchsia_sysmem_Allocator_Name,
    nullptr,
};

static constexpr zx_service_ops_t sysmem2_ops = {
    .init = sysmem2_init,
    .connect = sysmem2_connect,
    .release = sysmem2_release,
};

static constexpr zx_service_provider_t sysmem2_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = sysmem2_services,
    .ops = &sysmem2_ops,
};

const zx_service_provider_t* sysmem2_get_service_provider() { return &sysmem2_service_provider; }
