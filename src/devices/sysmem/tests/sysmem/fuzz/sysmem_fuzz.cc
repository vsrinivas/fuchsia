// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <src/devices/sysmem/drivers/sysmem/device.h>
#include <src/devices/sysmem/drivers/sysmem/driver.h>

#include "src/devices/sysmem/tests/sysmem/fuzz/sysmem_fuzz_common.h"

#define DBGRTN 0

#define LOGRTN(status, ...)           \
  {                                   \
    if (status != ZX_OK) {            \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }
#define LOGRTNC(condition, ...)       \
  {                                   \
    if ((condition)) {                \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }

extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
  const size_t kRequiredFuzzingBytes = sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints);

  LOGRTNC(size != kRequiredFuzzingBytes, "size: %zu != kRequiredFuzzingBytes: %zu\n", size,
          kRequiredFuzzingBytes);
  FakeDdkSysmem fake_sysmem;
  LOGRTNC(!fake_sysmem.Init(), "Failed FakeDdkSysmem::Init()\n");

  auto allocator_client = fake_sysmem.Connect();
  LOGRTN(allocator_client.status_value(), "Failed to connect to sysmem driver.\n");
  fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator(std::move(allocator_client.value()));

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  LOGRTN(token_endpoints.status_value(), "Failed token channel create.\n");
  auto [token_client_end, token_server_end] = std::move(*token_endpoints);

  auto allocate_result = allocator.AllocateSharedCollection(std::move(token_server_end));
  LOGRTN(allocate_result.status(), "Failed to allocate shared collection.\n");

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  LOGRTN(collection_endpoints.status_value(), "Failed collection channel create.\n");
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  auto bind_result =
      allocator.BindSharedCollection(std::move(token_client_end), std::move(collection_server_end));
  LOGRTN(bind_result.status(), "Failed to bind shared collection.\n");

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  memcpy(&constraints, data, kRequiredFuzzingBytes);

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection(
      std::move(collection_client_end));
  auto set_constraints_result = collection.SetConstraints(true, std::move(constraints));
  LOGRTN(set_constraints_result.status(), "Failed to set buffer collection constraints.\n");

  fidl::WireResult result = collection.WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be
  // due to any step above failing async.
  LOGRTN(result.status(), "Failed on WaitForBuffersAllocated.\n");
  LOGRTN(result->status, "Bad allocation_status on WaitForBuffersAllocated.\n");

  return 0;
}
