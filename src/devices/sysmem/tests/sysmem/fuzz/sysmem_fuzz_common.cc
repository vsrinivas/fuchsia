// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysmem_fuzz_common.h"

#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "log_rtn.h"

FakeDdkSysmem::~FakeDdkSysmem() {
  if (initialized_) {
    sysmem_.DdkAsyncRemove();
    ZX_ASSERT(ZX_OK == ddk_.WaitUntilRemove());
    sysmem_.ResetThreadCheckerForTesting();
    ZX_ASSERT(sysmem_.logical_buffer_collections().size() == 0);
    ZX_ASSERT(ddk_.Ok());
    initialized_ = false;
  }
}
bool FakeDdkSysmem::Init() {
  if (initialized_) {
    fprintf(stderr, "FakeDdkSysmem already initialized.\n");
    fflush(stderr);
    return false;
  }
  // Avoid wasting fuzzer time outputting logs.
  fake_ddk::kMinLogSeverity = FX_LOG_FATAL;
  // Pick a platform where AFBC textures will be used. Also add a protected pool to test code that
  // handles that specially (though protected allocations will always fail because the pool is never
  // marked ready).
  static const sysmem_metadata_t metadata{
      .vid = PDEV_VID_AMLOGIC,
      .pid = PDEV_PID_AMLOGIC_S905D2,
      .protected_memory_size = 1024 * 1024,
      .contiguous_memory_size = 0,
  };
  ddk_.SetMetadata(SYSMEM_METADATA_TYPE, &metadata, sizeof(metadata));

  pdev_.UseFakeBti();

  ddk_.SetProtocol(ZX_PROTOCOL_PBUS, pbus_.proto());
  ddk_.SetProtocol(ZX_PROTOCOL_PDEV, pdev_.proto());
  if (ZX_OK == sysmem_.Bind()) {
    initialized_ = true;
  }
  sysmem_.set_settings(sysmem_driver::Settings{.max_allocation_size = 256 * 1024});
  return initialized_;
}

zx::status<fidl::ClientEnd<fuchsia_sysmem::Allocator>> FakeDdkSysmem::Connect() {
  zx::status allocator_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::Allocator>();
  if (allocator_endpoints.is_error()) {
    return zx::error(allocator_endpoints.status_value());
  }

  auto [allocator_client_end, allocator_server_end] = std::move(*allocator_endpoints);

  fidl::WireResult result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_sysmem::DriverConnector>(
                                               zx::unowned(ddk_.FidlClient())))
                                .Connect(std::move(allocator_server_end));
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::move(allocator_client_end));
}
