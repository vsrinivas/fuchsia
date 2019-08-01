// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/kcounter/provider.h>
#include <lib/syslog/global.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "kcounter.h"

namespace {

zx_status_t GetInspectVMO(void* ctx, fidl_txn_t* txn) {
  zx::vmo vmo;
  fuchsia_mem_Buffer buffer{.vmo = ZX_HANDLE_INVALID, .size = 0};
  auto status = static_cast<kcounter::VmoToInspectMapper*>(ctx)->GetInspectVMO(&vmo);
  if (status == ZX_OK) {
    buffer.vmo = vmo.get();
    auto size_status = vmo.get_size(&buffer.size);
    if (size_status != ZX_OK) {
      return size_status;
    }
  }
  return fuchsia_kernel_CounterGetInspectVMO_reply(txn, status, &buffer);
}

zx_status_t UpdateInspectVMO(void* ctx, fidl_txn_t* txn) {
  auto status = static_cast<kcounter::VmoToInspectMapper*>(ctx)->UpdateInspectVMO();
  return fuchsia_kernel_CounterUpdateInspectVMO_reply(txn, status);
}

constexpr fuchsia_kernel_Counter_ops_t kFidlOps = {
    .GetInspectVMO = GetInspectVMO,
    .UpdateInspectVMO = UpdateInspectVMO,
};

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                    zx_handle_t request) {
  if (strcmp(service_name, fuchsia_kernel_Counter_Name) == 0) {
    return fidl_bind(dispatcher, request,
                     reinterpret_cast<fidl_dispatch_t*>(fuchsia_kernel_Counter_dispatch), ctx,
                     &kFidlOps);
  }

  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Init(void** out_ctx) {
  *out_ctx = new kcounter::VmoToInspectMapper;
  return ZX_OK;
}

void Release(void* ctx) { delete static_cast<kcounter::VmoToInspectMapper*>(ctx); }

constexpr const char* kKcounterServices[] = {
    fuchsia_kernel_Counter_Name,
    nullptr,
};

constexpr zx_service_ops_t kKcounterOps = {
    .init = Init,
    .connect = Connect,
    .release = Release,
};

constexpr zx_service_provider_t kcounter_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kKcounterServices,
    .ops = &kKcounterOps,
};

}  // namespace

const zx_service_provider_t* kcounter_get_service_provider() { return &kcounter_service_provider; }
