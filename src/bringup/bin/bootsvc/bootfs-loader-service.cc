// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs-loader-service.h"

#include <zircon/boot/bootfs.h>

namespace bootsvc {

const loader_service_ops_t BootfsLoaderService::kOps_ = {
    .load_object = BootfsLoaderService::LoadObject,
    .load_abspath = BootfsLoaderService::LoadAbspath,
    .publish_data_sink = BootfsLoaderService::PublishDataSink,
    .finalizer = nullptr,
};

zx_status_t BootfsLoaderService::LoadObject(const char* name, zx::vmo* vmo_out) {
  char tmp[ZBI_BOOTFS_MAX_NAME_LEN];
  if (snprintf(tmp, sizeof(tmp), "lib/%s", name) >= static_cast<int>(sizeof(tmp))) {
    return ZX_ERR_BAD_PATH;
  }
  uint64_t size;
  return bootfs_->Open(tmp, /*executable=*/true, vmo_out, &size);
}

zx_status_t BootfsLoaderService::LoadAbspath(const char* name, zx::vmo* vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t BootfsLoaderService::PublishDataSink(const char* name, zx::vmo vmo) {
  return ZX_ERR_NOT_SUPPORTED;
}

BootfsLoaderService::BootfsLoaderService(fbl::RefPtr<BootfsService> svc)
    : bootfs_(std::move(svc)) {}

zx_status_t BootfsLoaderService::Create(fbl::RefPtr<BootfsService> svc,
                                        async_dispatcher_t* dispatcher,
                                        fbl::RefPtr<BootfsLoaderService>* loader_out) {
  auto ldsvc = fbl::AdoptRef(new BootfsLoaderService(svc));
  // loader_service_create points to ldsvc, so we must cal
  // loader_service_release before dropping out reference to it.
  zx_status_t status = loader_service_create(dispatcher, &kOps_, ldsvc.get(), &ldsvc->loader_);
  if (status != ZX_OK) {
    return status;
  }
  *loader_out = std::move(ldsvc);
  return ZX_OK;
}

BootfsLoaderService::~BootfsLoaderService() {
  if (loader_) {
    loader_service_release(loader_);
  }
}

zx_status_t BootfsLoaderService::Connect(zx::channel* out) {
  return loader_service_connect(loader_, out->reset_and_get_address());
}

}  // namespace bootsvc
