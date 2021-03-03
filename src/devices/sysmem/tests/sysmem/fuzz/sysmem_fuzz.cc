// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async-2/fidl_struct.h>

#include <src/devices/sysmem/drivers/sysmem/device.h>
#include <src/devices/sysmem/drivers/sysmem/driver.h>

#include "src/devices/sysmem/tests/sysmem/fuzz/sysmem_fuzz_common.h"

using BufferCollectionConstraints = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                               fuchsia_sysmem::wire::BufferCollectionConstraints>;
using BufferCollectionInfo =
    FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2, fuchsia_sysmem::wire::BufferCollectionInfo_2>;

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
  const size_t kRequiredFuzzingBytes = sizeof(fuchsia_sysmem_BufferCollectionConstraints);

  LOGRTNC(size != kRequiredFuzzingBytes, "size: %zu != kRequiredFuzzingBytes: %zu\n", size,
          kRequiredFuzzingBytes);
  FakeDdkSysmem fake_sysmem;
  LOGRTNC(!fake_sysmem.Init(), "Failed FakeDdkSysmem::Init()\n");

  zx::channel allocator_client;
  zx_status_t status =
      connect_to_sysmem_driver(fake_sysmem.ddk().FidlClient().get(), &allocator_client);
  LOGRTN(status, "Failed to connect to sysmem driver.\n");

  zx::channel token_server, token_client;
  status = zx::channel::create(0u, &token_server, &token_client);
  LOGRTN(status, "Failed token channel create.\n");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  LOGRTN(status, "Failed to allocate shared collection.\n");

  zx::channel collection_server, collection_client;
  status = zx::channel::create(0, &collection_client, &collection_server);
  LOGRTN(status, "Failed collection channel create.\n");

  LOGRTNC(token_client.get() == ZX_HANDLE_INVALID, "Invalid token_client handle.\n");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  LOGRTN(status, "Failed to bind shared collection.\n");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  memcpy(constraints.get(), data, kRequiredFuzzingBytes);

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  LOGRTN(status, "Failed to set buffer collection constraints.\n");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  LOGRTN(status, "Failed on WaitForBuffersAllocated.\n");
  LOGRTN(allocation_status, "Bad allocation_status on WaitForBuffersAllocated.\n");

  return 0;
}
