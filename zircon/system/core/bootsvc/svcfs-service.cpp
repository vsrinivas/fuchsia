// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svcfs-service.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "util.h"

namespace {

zx_status_t RootResourceGet(void* ctx, fidl_txn_t* txn) {
    zx::resource resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
    if (!resource.is_valid()) {
        return ZX_ERR_NOT_FOUND;
    }
    return fuchsia_boot_RootResourceGet_reply(txn, resource.release());
}

constexpr fuchsia_boot_RootResource_ops fidl_ops = {
    .Get = RootResourceGet,
};

} // namespace

namespace bootsvc {

fbl::RefPtr<SvcfsService> SvcfsService::Create(async_dispatcher_t* dispatcher) {
    return fbl::AdoptRef(new SvcfsService(dispatcher));
}

SvcfsService::SvcfsService(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher), root_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void SvcfsService::AddService(const char* service_name, fbl::RefPtr<fs::Service> service) {
    root_->AddEntry(service_name, std::move(service));
}

zx_status_t SvcfsService::CreateRootConnection(zx::channel* out) {
    return CreateVnodeConnection(&vfs_, root_, out);
}

fbl::RefPtr<fs::Service> CreateRootResourceService(async_dispatcher_t* dispatcher) {
    return fbl::MakeRefCounted<fs::Service>([dispatcher](zx::channel channel) {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootResource_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, nullptr, &fidl_ops);
    });
}

} // namespace bootsvc
