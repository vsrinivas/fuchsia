// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/kernel-debug/kernel-debug.h>
#include <lib/ktrace/ktrace.h>
#include <lib/profile/profile.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/job.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iterator>
#include <string_view>

#include <crashsvc/crashsvc.h>
#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "src/bringup/bin/svchost/args.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"
#include "sysmem.h"

namespace {
zx::result<zx::job> GetRootJob(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root) {
  zx::result client = component::ConnectAt<fuchsia_kernel::RootJob>(svc_root);
  if (client.is_error()) {
    fprintf(stderr, "svchost: unable to connect to fuchsia.kernel.RootJob\n");
    return client.take_error();
  }
  fidl::WireResult result = fidl::WireCall(client.value())->Get();
  if (!result.ok()) {
    fprintf(stderr, "svchost: unable to get root job\n");
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().job));
}
zx::result<zx::resource> GetRootResource(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root) {
  zx::result client = component::ConnectAt<fuchsia_boot::RootResource>(svc_root);
  if (client.is_error()) {
    fprintf(stderr, "svchost: unable to connect to fuchsia.boot.RootResource\n");
    return client.take_error();
  }

  fidl::WireResult result = fidl::WireCall(client.value())->Get();
  if (!result.ok()) {
    fprintf(stderr, "svchost: unable to get root resource\n");
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().resource));
}
}  // namespace

// An instance of a zx_service_provider_t.
//
// Includes the |ctx| pointer for the zx_service_provider_t.
using zx_service_provider_instance_t = struct zx_service_provider_instance {
  // The service provider for which this structure is an instance.
  const zx_service_provider_t* provider;

  // The loop on which the service provider runs.
  async_loop_t* loop = nullptr;

  // The thread on which the service provider runs.
  thrd_t thread = {};

  // The |ctx| pointer returned by the provider's |init| function, if any.
  void* ctx;
};

static zx_status_t provider_init(zx_service_provider_instance_t* instance) {
  zx_status_t status = async_loop_create(&kAsyncLoopConfigNeverAttachToThread, &instance->loop);
  if (status != ZX_OK) {
    return status;
  }

  status =
      async_loop_start_thread(instance->loop, instance->provider->services[0], &instance->thread);
  if (status != ZX_OK) {
    return status;
  }

  if (instance->provider->ops->init) {
    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(instance->loop);
    status = async::PostTask(dispatcher, [instance]() {
      auto status = instance->provider->ops->init(&instance->ctx);
      ZX_ASSERT(status == ZX_OK);
    });
    if (status != ZX_OK) {
      async_loop_destroy(instance->loop);
      return status;
    }
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
        fbl::MakeRefCounted<fs::Service>([instance, service_name](zx::channel request) {
          async_dispatcher_t* dispatcher = async_loop_get_dispatcher(instance->loop);
          return async::PostTask(dispatcher, [instance, dispatcher, service_name,
                                              request = std::move(request)]() mutable {
            instance->provider->ops->connect(instance->ctx, dispatcher, service_name,
                                             request.release());
          });
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
  if (instance->provider->ops->release) {
    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(instance->loop);
    async::PostTask(dispatcher, [instance]() { instance->provider->ops->release(instance->ctx); });
  }
  async_loop_destroy(instance->loop);
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
  StdoutToDebuglog::Init();

  fbl::unique_fd svc_root(open("/svc", O_RDWR | O_DIRECTORY));
  fdio_cpp::UnownedFdioCaller caller(svc_root.get());

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  svc::Outgoing outgoing(dispatcher);

  // Parse boot arguments.
  zx::result boot_args = component::ConnectAt<fuchsia_boot::Arguments>(caller.directory());
  if (boot_args.is_error()) {
    fprintf(stderr, "svchost: unable to connect to fuchsia.boot.Arguments: %s\n",
            boot_args.status_string());
    return 1;
  }
  svchost::Arguments args;
  zx_status_t status = svchost::ParseArgs(boot_args.value(), &args);
  if (status != ZX_OK) {
    fprintf(stderr, "svchost: unable to read args: %s", zx_status_get_string(status));
    return 1;
  }

  // Get the root job.
  zx::job root_job;
  {
    auto res = GetRootJob(caller.directory());
    if (!res.is_ok()) {
      fprintf(stderr, "svchost: error: Failed to get root job: %s\n", zx_status_get_string(status));
      return 1;
    }
    root_job = std::move(res.value());
  }

  // Get the root resource.
  zx::resource root_resource;
  {
    auto res = GetRootResource(caller.directory());
    if (!res.is_ok()) {
      fprintf(stderr, "svchost: error: Failed to get root resource: %s\n",
              zx_status_get_string(status));
      return 1;
    }
    root_resource = std::move(res.value());
  }

  status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    fprintf(stderr, "svchost: error: Failed to serve outgoing directory: %d (%s).\n", status,
            zx_status_get_string(status));
    return 1;
  }

  zx_handle_t profile_root_job_copy;
  status = zx_handle_duplicate(root_job.get(), ZX_RIGHT_SAME_RIGHTS, &profile_root_job_copy);
  if (status != ZX_OK) {
    fprintf(stderr, "svchost: failed to duplicate root job: %d (%s).\n", status,
            zx_status_get_string(status));
    return 1;
  }

  zx_service_provider_instance_t service_providers[] = {
      {.provider = sysmem2_get_service_provider(), .ctx = nullptr},
      {
          .provider = kernel_debug_get_service_provider(),
          .ctx = reinterpret_cast<void*>(static_cast<uintptr_t>(root_resource.get())),
      },
      {
          .provider = profile_get_service_provider(),
          .ctx = reinterpret_cast<void*>(static_cast<uintptr_t>(profile_root_job_copy)),
      },
      {
          .provider = ktrace_get_service_provider(),
          .ctx = reinterpret_cast<void*>(static_cast<uintptr_t>(root_resource.release())),
      },
  };

  for (size_t i = 0; i < std::size(service_providers); ++i) {
    status = provider_load(&service_providers[i], dispatcher, outgoing.svc_dir());
    if (status != ZX_OK) {
      fprintf(stderr, "svchost: error: Failed to load service provider %zu: %d (%s).\n", i, status,
              zx_status_get_string(status));
      return 1;
    }
  }

  thrd_t thread;
  status =
      start_crashsvc(std::move(root_job),
                     args.require_system ? caller.borrow_channel() : ZX_HANDLE_INVALID, &thread);
  if (status != ZX_OK) {
    // The system can still function without crashsvc, log the error but
    // keep going.
    fprintf(stderr, "svchost: error: Failed to start crashsvc: %d (%s).\n", status,
            zx_status_get_string(status));
  } else {
    thrd_detach(thread);
  }

  status = loop.Run();

  for (auto& service_provider : service_providers) {
    provider_release(&service_provider);
  }

  return status;
}
