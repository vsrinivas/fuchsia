// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "sysmem_fuzz_common.h"

#include <zircon/device/sysmem.h>

#include <ddk/platform-defs.h>

#include "log_rtn.h"

FakeDdkSysmem::~FakeDdkSysmem() {
  if (initialized_) {
    // Run loop until idle to ensure all pending VMO closures are processed.
    sysmem_.RunLoopUntilIdle();
    sysmem_.DdkAsyncRemove();
    ZX_ASSERT(ZX_OK == ddk_.WaitUntilRemove());
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
  // Pick a platform where AFBC textures will be used.
  static const sysmem_metadata_t metadata{
      .vid = PDEV_VID_AMLOGIC,
      .pid = PDEV_PID_AMLOGIC_S905D2,
      .protected_memory_size = 0,
      .contiguous_memory_size = 0,
  };
  ddk_.SetMetadata(&metadata, sizeof(metadata));

  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[2], 2);
  protocols[0] = {ZX_PROTOCOL_PBUS, *reinterpret_cast<const fake_ddk::Protocol*>(pbus_.proto())};
  protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
  ddk_.SetProtocols(std::move(protocols));
  if (ZX_OK == sysmem_.Bind()) {
    initialized_ = true;
  }
  sysmem_.set_settings(sysmem_driver::Settings{.max_allocation_size = 256 * 1024});
  return initialized_;
}

zx_status_t connect_to_sysmem_driver(zx_handle_t fake_ddk_client,
                                     zx::channel* allocator_client_param) {
  zx::channel allocator_client;
  zx::channel allocator_server;
  zx_status_t status = zx::channel::create(0, &allocator_client, &allocator_server);
  LOGRTN(status, "Failed allocator channel create.\n");

  status = fuchsia_sysmem_DriverConnectorConnect(fake_ddk_client, allocator_server.release());
  LOGRTN(status, "Failed sysmem driver connect.\n");

  *allocator_client_param = std::move(allocator_client);
  return ZX_OK;
}
