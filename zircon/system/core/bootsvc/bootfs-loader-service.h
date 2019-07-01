// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_BOOTSVC_BOOTFS_LOADER_SERVICE_H_
#define ZIRCON_SYSTEM_CORE_BOOTSVC_BOOTFS_LOADER_SERVICE_H_

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/vmo.h>
#include <loader-service/loader-service.h>

#include "bootfs-service.h"

namespace bootsvc {

class BootfsLoaderService : public fbl::RefCounted<BootfsService> {
 public:
  ~BootfsLoaderService();

  BootfsLoaderService(const BootfsLoaderService&) = delete;
  BootfsLoaderService& operator=(const BootfsLoaderService&) = delete;
  BootfsLoaderService(BootfsLoaderService&&) = delete;
  BootfsLoaderService& operator=(BootfsLoaderService&&) = delete;

  // Create a loader that loads from the given bootfs service and dispatches
  // on the given dispatcher.
  static zx_status_t Create(fbl::RefPtr<BootfsService> svc, async_dispatcher_t* dispatcher,
                            fbl::RefPtr<BootfsLoaderService>* loader_out);

  // Returns a dl_set_loader_service-compatible loader service
  zx_status_t Connect(zx::channel* out);

 private:
  explicit BootfsLoaderService(fbl::RefPtr<BootfsService> svc);

  zx_status_t LoadObject(const char* name, zx::vmo* vmo_out);
  zx_status_t LoadAbspath(const char* name, zx::vmo* vmo_out);
  zx_status_t PublishDataSink(const char* name, zx::vmo vmo);

  static zx_status_t LoadObject(void* ctx, const char* name, zx_handle_t* vmo_out) {
    zx::vmo vmo;
    zx_status_t status = reinterpret_cast<BootfsLoaderService*>(ctx)->LoadObject(name, &vmo);
    *vmo_out = vmo.release();
    return status;
  }

  static zx_status_t LoadAbspath(void* ctx, const char* name, zx_handle_t* vmo_out) {
    zx::vmo vmo;
    zx_status_t status = reinterpret_cast<BootfsLoaderService*>(ctx)->LoadAbspath(name, &vmo);
    *vmo_out = vmo.release();
    return status;
  }

  static zx_status_t PublishDataSink(void* ctx, const char* name, zx_handle_t vmo) {
    return reinterpret_cast<BootfsLoaderService*>(ctx)->PublishDataSink(name, zx::vmo(vmo));
  }

  static const loader_service_ops_t kOps_;

  fbl::RefPtr<BootfsService> bootfs_;
  loader_service_t* loader_ = nullptr;
};

}  // namespace bootsvc

#endif  // ZIRCON_SYSTEM_CORE_BOOTSVC_BOOTFS_LOADER_SERVICE_H_
