// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/kcounter/provider.h>
#include <lib/paver/provider.h>
#include <lib/svc/outgoing.h>
#include <zircon/status.h>

#include <iterator>

#include <fbl/algorithm.h>

#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

// An instance of a zx_service_provider_t.
//
// Includes the |ctx| pointer for the zx_service_provider_t.
typedef struct zx_service_provider_instance {
  // The service provider for which this structure is an instance.
  const zx_service_provider_t* provider;

  // The |ctx| pointer returned by the provider's |init| function, if any.
  void* ctx;
} zx_service_provider_instance_t;

static zx_status_t provider_init(zx_service_provider_instance_t* instance) {
  if (instance->provider->ops->init) {
    zx_status_t status = instance->provider->ops->init(&instance->ctx);
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

static zx_status_t provider_publish(zx_service_provider_instance_t* instance,
                                    async_dispatcher_t* dispatcher,
                                    const fbl::RefPtr<fs::PseudoDir>& dir) {
  const zx_service_provider_t* provider = instance->provider;

  if (!provider->services || !provider->ops->connect)
    return ZX_ERR_INVALID_ARGS;

  for (size_t i = 0; provider->services[i]; ++i) {
    const char* service_name = provider->services[i];
    zx_status_t status = dir->AddEntry(
        service_name,
        fbl::MakeRefCounted<fs::Service>([instance, dispatcher, service_name](zx::channel request) {
          return instance->provider->ops->connect(instance->ctx, dispatcher, service_name,
                                                  request.release());
        }));
    if (status != ZX_OK) {
      for (size_t j = 0; j < i; ++j)
        dir->RemoveEntry(provider->services[j]);
      return status;
    }
  }

  return ZX_OK;
}

static void provider_release(zx_service_provider_instance_t* instance) {
  if (instance->provider->ops->release)
    instance->provider->ops->release(instance->ctx);
  instance->ctx = nullptr;
}

static zx_status_t provider_load(zx_service_provider_instance_t* instance,
                                 async_dispatcher_t* dispatcher,
                                 const fbl::RefPtr<fs::PseudoDir>& dir) {
  if (instance->provider->version != SERVICE_PROVIDER_VERSION) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = provider_init(instance);
  if (status != ZX_OK) {
    return status;
  }

  status = provider_publish(instance, dispatcher, dir);
  if (status != ZX_OK) {
    provider_release(instance);
    return status;
  }

  return ZX_OK;
}

int main(int argc, char** argv) {
  zx_status_t status = StdoutToDebuglog::Init();
  if (status != ZX_OK) {
    printf("Failed to redirect stdout to debuglog, assuming test environment and continuing\n");
  }
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  svc::Outgoing outgoing(dispatcher);

  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    fprintf(stderr, "miscsvc: error: Failed to serve outgoing directory: %d (%s).\n", status,
            zx_status_get_string(status));
    return 1;
  }

  zx_service_provider_instance_t service_providers[] = {
      {.provider = paver_get_service_provider(), .ctx = nullptr},
      {.provider = kcounter_get_service_provider(), .ctx = nullptr},
  };

  for (size_t i = 0; i < std::size(service_providers); ++i) {
    status = provider_load(&service_providers[i], dispatcher, outgoing.svc_dir());
    if (status != ZX_OK) {
      fprintf(stderr, "miscsvc: error: Failed to load service provider %zu: %d (%s).\n", i, status,
              zx_status_get_string(status));
      return 1;
    }
  }

  status = loop.Run();

  for (size_t i = 0; i < std::size(service_providers); ++i) {
    provider_release(&service_providers[i]);
  }

  return status;
}
