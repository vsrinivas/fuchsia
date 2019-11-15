// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>

#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "zircon_platform_trace_provider.h"

namespace magma {

#if MAGMA_ENABLE_TRACING
bool InitializeTraceProviderWithFdio(PlatformTraceProvider* provider) {
  DASSERT(provider);
  zx::channel client_channel, server_channel;
  zx_status_t status = zx::channel::create(0, &client_channel, &server_channel);
  if (status != ZX_OK)
    return DRETF(false, "Could allocate channel: %d", status);
  status = fdio_service_connect("/svc/fuchsia.tracing.provider.Registry", server_channel.release());
  if (status != ZX_OK)
    return DRETF(false, "Could not connect to tracing provider register: %d", status);
  return provider->Initialize(client_channel.release());
}

#else

bool InitializeTraceProviderWithFdio(PlatformTraceProvider* provider) { return false; }

#endif

}  // namespace magma
