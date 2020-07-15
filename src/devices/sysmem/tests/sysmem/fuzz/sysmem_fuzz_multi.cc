// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "log_rtn.h"
#include "sysmem_fuzz_common.h"

// Multiple Participants
extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
  const size_t kBufferCollectionConstraintsSize =
      sizeof(fuchsia_sysmem_BufferCollectionConstraints);
  LOGRTNC(size != 2 * kBufferCollectionConstraintsSize,
          "size: %zu != 2 * kBufferCollectionConstraintsSize: %zu\n", size,
          kBufferCollectionConstraintsSize);
  uint8_t* data_ptr = data;

  FakeDdkSysmem fake_sysmem;
  LOGRTNC(!fake_sysmem.Init(), "Failed FakeDdkSysmem::Init()\n");

  zx::channel allocator_client_1;
  zx_status_t status =
      connect_to_sysmem_driver(fake_sysmem.ddk().FidlClient().get(), &allocator_client_1);
  LOGRTN(status, "Failed to connect to sysmem driver.\n");

  zx::channel token_client_1;
  zx::channel token_server_1;
  status = zx::channel::create(0, &token_client_1, &token_server_1);
  LOGRTN(status, "Failed token 1 channel create.\n");

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client_1.get(),
                                                            token_server_1.release());
  LOGRTN(status, "Failed client 1 shared collection allocate.\n");

  zx::channel token_client_2;
  zx::channel token_server_2;
  status = zx::channel::create(0, &token_client_2, &token_server_2);
  LOGRTN(status, "Failed token 2 channel create.\n");

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_2.release());
  LOGRTN(status, "Failed token 1 -> 2 duplicate.\n");

  zx::channel token_client_3;
  zx::channel token_server_3;
  status = zx::channel::create(0, &token_client_3, &token_server_3);
  LOGRTN(status, "Failed token 3 channel create.\n");

  // Client 3 is used to test a participant that doesn't set any constraints
  // and only wants a notification that the allocation is done.
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_3.release());
  LOGRTN(status, "Failed token 1 -> 3 duplicate.\n");

  zx::channel collection_client_1;
  zx::channel collection_server_1;
  status = zx::channel::create(0, &collection_client_1, &collection_server_1);
  LOGRTN(status, "Failed collection 1 channel create.\n");
  LOGRTNC(token_client_1.get() == ZX_HANDLE_INVALID, "Invalid token client 1.\n");

  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client_1.get(), token_client_1.release(), collection_server_1.release());
  LOGRTN(status, "Bind shared collection client/collection 1.\n");

  BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
  memcpy(constraints_1.get(), data_ptr, kBufferCollectionConstraintsSize);
  data_ptr += kBufferCollectionConstraintsSize;

  BufferCollectionConstraints constraints_2(BufferCollectionConstraints::Default);
  memcpy(constraints_2.get(), data_ptr, kBufferCollectionConstraintsSize);
  data_ptr += kBufferCollectionConstraintsSize;

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                         constraints_1.release());
  LOGRTN(status, "BufferCollectionSetConstraints 1 failed.\n");

  // Client 2 connects to sysmem separately.
  zx::channel allocator_client_2;
  status = connect_to_sysmem_driver(fake_sysmem.ddk().FidlClient().get(), &allocator_client_2);
  LOGRTN(status, "Failed to connect to sysmem driver (2).\n");

  zx::channel collection_client_2, collection_server_2;
  status = zx::channel::create(0, &collection_client_2, &collection_server_2);
  LOGRTN(status, "Failed collection 2 channel create.\n");

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
  LOGRTN(status, "Failed BufferCollectionSync 1.\n");

  // ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client_2.get(), token_client_2.release(), collection_server_2.release());
  LOGRTN(status, "Failed BindSharedCollection 2.\n");

  zx::channel collection_client_3;
  zx::channel collection_server_3;
  status = zx::channel::create(0, &collection_client_3, &collection_server_3);
  LOGRTN(status, "Failed collection 3 channel create.\n");
  LOGRTNC(token_client_3.get() == ZX_HANDLE_INVALID, "Invalid token client 3.\n");

  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client_2.get(), token_client_3.release(), collection_server_3.release());
  LOGRTN(status, "Failed BindSharedCollection 2 -> 3.\n");

  fuchsia_sysmem_BufferCollectionConstraints empty_constraints = {};

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_3.get(), false,
                                                         &empty_constraints);
  LOGRTN(status, "Failed BufferCollectionSetConstraints 3 -> empty.\n");

  // Not all constraints have been input, so the buffers haven't been
  // allocated yet.
  zx_status_t check_status;
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(),
                                                                &check_status);
  LOGRTN(status, "Failed BufferCollectionCheckBuffersAllocated 1.\n");
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(),
                                                                &check_status);
  LOGRTN(status, "Failed BufferCollectionCheckBuffersAllocated 2.\n");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                         constraints_2.release());
  LOGRTN(status, "Failed BufferCollectionSetConstraints 2.\n");

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //
  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info_1(BufferCollectionInfo::Default);
  // This helps with a later exact equality check.
  memset(buffer_collection_info_1.get(), 0, sizeof(*buffer_collection_info_1.get()));
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_1.get(), &allocation_status, buffer_collection_info_1.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  LOGRTN(status, "WaitForBuffersAllocated, collection 1 failed.\n");
  LOGRTN(allocation_status, "WaitForBuffersAllocated, allocation_status collection 1 failed.\n");

  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(),
                                                                &check_status);
  LOGRTN(status, "CheckBuffersAllocated, collection 1 failed.\n");
  LOGRTN(check_status, "CheckBuffersAllocated, check_status collection 1 failed.\n");
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(),
                                                                &check_status);
  LOGRTN(status, "CheckBuffersAllocated, collection 2 failed.\n");
  LOGRTN(check_status, "CheckBuffersAllocated, check_status collection 2 failed.\n");

  BufferCollectionInfo buffer_collection_info_2(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_2.get(), &allocation_status, buffer_collection_info_2.get());
  LOGRTN(status, "WaitForBuffersAllocated, collection 2 failed.\n");
  LOGRTN(allocation_status, "WaitForBuffersAllocated, allocation_status collection 2 failed.\n");

  BufferCollectionInfo buffer_collection_info_3(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_3.get(), &allocation_status, buffer_collection_info_3.get());
  LOGRTN(status, "WaitForBuffersAllocated, collection 3 failed.\n");
  LOGRTN(allocation_status, "WaitForBuffersAllocated, allocation_status collection 3 failed.\n");

  // Close to ensure grabbing null constraints from a closed collection
  // doesn't crash
  zx_status_t close_status = fuchsia_sysmem_BufferCollectionClose(collection_client_3.get());
  LOGRTN(close_status, "Failed to close collection_client_3.\n");

  return 0;
}
