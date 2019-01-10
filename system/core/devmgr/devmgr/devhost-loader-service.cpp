// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost-loader-service.h"

#include <array>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>

#include <fbl/string_printf.h>
#include <lib/fdio/io.h>

#include "coordinator.h"
#include "../shared/fdio.h"

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
    zx_status_t status = fdio_get_vmo_clone(fd.get(), vmo);
    if (status != ZX_OK) {
        return status;
    }
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

} // namespace

namespace devmgr {

zx_status_t DevhostLoaderService::Init() {
    zx_status_t status = fdio_ns_create(&ns_);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to create namespace %d\n", status);
        return status;
    }
    status = fdio_ns_bind(ns_, "/boot", fs_clone("boot").release());
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to bind namespace %d\n", status);
        return status;
    }
    root_.reset(fdio_ns_opendir(ns_));
    if (!root_) {
        fprintf(stderr, "devmgr: failed to open root directory %d\n", errno);
        return ZX_ERR_IO;
    }
    status = loader_service_create(DcAsyncDispatcher(), &ops_, this, &svc_);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to create loader service %d\n", status);
        return status;
    }
    return ZX_OK;
}

DevhostLoaderService::~DevhostLoaderService() {
    if (svc_ != nullptr) {
        loader_service_release(svc_);
    }
    if (ns_ != nullptr) {
        fdio_ns_destroy(ns_);
    }
}

zx_status_t DevhostLoaderService::Connect(zx::channel* out) {
    return loader_service_connect(svc_, out->reset_and_get_address());
}

} // namespace devmgr
