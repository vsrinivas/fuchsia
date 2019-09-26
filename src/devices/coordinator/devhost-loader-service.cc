// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost-loader-service.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fit/defer.h>
#include <stdint.h>

#include <array>

#include <fbl/string_printf.h>

#include "coordinator.h"
#include "fdio.h"
#include "system-instance.h"

namespace {

static constexpr std::array<const char*, 3> kDriverWhitelist{
    "libasync-default.so",
    "libdriver.so",
    "libfdio.so",
};

// Check if the driver is in the whitelist.
bool InWhitelist(const char* name) {
  for (const char* driver : kDriverWhitelist) {
    if (strcmp(driver, name) == 0) {
      return true;
    }
  }
  return false;
}

zx_status_t LoadObject(void* ctx, const char* name, zx_handle_t* vmo) {
  if (!InWhitelist(name)) {
    return ZX_ERR_ACCESS_DENIED;
  }
  auto self = static_cast<devmgr::DevhostLoaderService*>(ctx);
  fbl::String path = fbl::StringPrintf("/boot/lib/%s", name);
  fbl::unique_fd fd(openat(self->root().get(), path.c_str(), O_RDONLY));
  if (!fd) {
    return ZX_ERR_NOT_FOUND;
  }
  zx::vmo nonexec_vmo;
  zx::vmo exec_vmo;
  zx_status_t status = fdio_get_vmo_clone(fd.get(), nonexec_vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  status = nonexec_vmo.replace_as_executable(zx::handle(), &exec_vmo);
  if (status != ZX_OK) {
    return status;
  }

  *vmo = exec_vmo.release();
  return zx_object_set_property(*vmo, ZX_PROP_NAME, path.c_str(), path.size());
}

zx_status_t LoadAbspath(void* ctx, const char* path, zx_handle_t* vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PublishDataSink(void* ctx, const char* name, zx_handle_t vmo) {
  zx_handle_close(vmo);
  return ZX_ERR_NOT_SUPPORTED;
}

constexpr loader_service_ops_t ops_{
    .load_object = LoadObject,
    .load_abspath = LoadAbspath,
    .publish_data_sink = PublishDataSink,
    .finalizer = nullptr,
};

}  // namespace

namespace devmgr {

zx_status_t DevhostLoaderService::Create(async_dispatcher_t* dispatcher,
                                         SystemInstance* system_instance,
                                         fbl::unique_ptr<DevhostLoaderService>* out) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_create(&ns);
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to create namespace %d\n", status);
    return status;
  }
  auto defer = fit::defer([ns] { fdio_ns_destroy(ns); });
  status = fdio_ns_bind(ns, "/boot", system_instance->CloneFs("boot").release());
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to bind namespace %d\n", status);
    return status;
  }
  fbl::unique_fd root(fdio_ns_opendir(ns));
  if (!root) {
    fprintf(stderr, "devcoordinator: failed to open root directory %d\n", errno);
    return ZX_ERR_IO;
  }
  fbl::unique_ptr<DevhostLoaderService> ldsvc(new DevhostLoaderService);
  status = loader_service_create(dispatcher, &ops_, ldsvc.get(), &ldsvc->svc_);
  if (status != ZX_OK) {
    fprintf(stderr, "devcoordinator: failed to create loader service %d\n", status);
    return status;
  }
  ldsvc->root_ = std::move(root);
  *out = std::move(ldsvc);
  return ZX_OK;
}

DevhostLoaderService::~DevhostLoaderService() {
  if (svc_ != nullptr) {
    loader_service_release(svc_);
  }
}

zx_status_t DevhostLoaderService::Connect(zx::channel* out) {
  return loader_service_connect(svc_, out->reset_and_get_address());
}

}  // namespace devmgr
