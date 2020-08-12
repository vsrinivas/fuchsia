// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/paver/provider.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/as370.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/paver.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/x64.h"

namespace {

zx_status_t Connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                    zx_handle_t request) {
  if (!strcmp(service_name, ::llcpp::fuchsia::paver::Paver::Name)) {
    auto* paver = reinterpret_cast<paver::Paver*>(ctx);
    paver->set_dispatcher(dispatcher);
    return fidl::BindSingleInFlightOnly(dispatcher, zx::channel(request), paver);
  }

  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Init(void** out_ctx) {
  *out_ctx = new paver::Paver;
  // NOTE: Ordering matters!
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::AstroPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::As370PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::SherlockPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::LuisPartitionerFactory>());
  paver::DevicePartitionerFactory::Register(
      std::make_unique<paver::ChromebookX64PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::X64PartitionerFactory>());
  paver::DevicePartitionerFactory::Register(std::make_unique<paver::DefaultPartitionerFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::AstroAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::SherlockAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::LuisAbrClientFactory>());
  abr::ClientFactory::Register(std::make_unique<paver::X64AbrClientFactory>());
  return ZX_OK;
}

void Release(void* ctx) { delete static_cast<paver::Paver*>(ctx); }

constexpr const char* kPaverServices[] = {
    ::llcpp::fuchsia::paver::Paver::Name,
    nullptr,
};

constexpr zx_service_ops_t kPaverOps = {
    .init = Init,
    .connect = Connect,
    .release = Release,
};

constexpr zx_service_provider_t paver_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = kPaverServices,
    .ops = &kPaverOps,
};

}  // namespace

const zx_service_provider_t* paver_get_service_provider() { return &paver_service_provider; }
