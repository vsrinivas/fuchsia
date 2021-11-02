// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "log_rtn.h"
#include "sysmem_fuzz_common.h"

// Multiple Participants
extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
  const size_t kBufferCollectionConstraintsSize =
      sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints);
  LOGRTNC(size != 2 * kBufferCollectionConstraintsSize,
          "size: %zu != 2 * kBufferCollectionConstraintsSize: %zu\n", size,
          kBufferCollectionConstraintsSize);
  uint8_t* data_ptr = data;

  FakeDdkSysmem fake_sysmem;
  LOGRTNC(!fake_sysmem.Init(), "Failed FakeDdkSysmem::Init()\n");

  auto allocator_client_1 = fake_sysmem.Connect();
  LOGRTN(allocator_client_1.status_value(), "Failed to connect to sysmem driver.\n");
  fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator_1(
      std::move(allocator_client_1.value()));

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  LOGRTN(token_endpoints.status_value(), "Failed token 1 channel create.\n");
  auto [token_client_1, token_server_1] = std::move(*token_endpoints);

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  auto new_collection_result = allocator_1->AllocateSharedCollection(std::move(token_server_1));
  LOGRTN(new_collection_result.status(), "Failed client 1 shared collection allocate.\n");

  zx::status token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  LOGRTN(token_endpoints_2.status_value(), "Failed token 2 channel create.\n");
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  fidl::WireSyncClient<fuchsia_sysmem::BufferCollectionToken> token_1(std::move(token_client_1));
  auto duplicate_result_2 = token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2));
  LOGRTN(duplicate_result_2.status(), "Failed token 1 -> 2 duplicate.\n");

  zx::status token_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  LOGRTN(token_endpoints_3.status_value(), "Failed token 3 channel create.\n");
  auto [token_client_3, token_server_3] = std::move(*token_endpoints_3);

  // Client 3 is used to test a participant that doesn't set any constraints
  // and only wants a notification that the allocation is done.
  auto duplicate_result_3 = token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_3));
  LOGRTN(duplicate_result_3.status(), "Failed token 1 -> 3 duplicate.\n");

  zx::status collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  LOGRTN(collection_endpoints_1.status_value(), "Failed collection 1 channel create.\n");
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  LOGRTNC(token_1.client_end().channel().get() == ZX_HANDLE_INVALID, "Invalid token client 1.\n");
  auto bind_result =
      allocator_1->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1));
  LOGRTN(bind_result.status(), "Bind shared collection client/collection 1.\n");

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  memcpy(&constraints_1, data_ptr, kBufferCollectionConstraintsSize);
  data_ptr += kBufferCollectionConstraintsSize;

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2;
  memcpy(&constraints_2, data_ptr, kBufferCollectionConstraintsSize);
  data_ptr += kBufferCollectionConstraintsSize;

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection_1(
      std::move(collection_client_1));
  auto set_constraints_result = collection_1->SetConstraints(true, std::move(constraints_1));
  LOGRTN(set_constraints_result.status(), "BufferCollectionSetConstraints 1 failed.\n");

  // Client 2 connects to sysmem separately.
  auto allocator_client_2 = fake_sysmem.Connect();
  LOGRTN(allocator_client_2.status_value(), "Failed to connect to sysmem driver. (2)\n");
  fidl::WireSyncClient<fuchsia_sysmem::Allocator> allocator_2(
      std::move(allocator_client_2.value()));

  zx::status collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  LOGRTN(collection_endpoints_2.status_value(), "Failed collection 2 channel create.\n");
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection_2(
      std::move(collection_client_2));

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  auto sync_result = collection_1->Sync();
  LOGRTN(sync_result.status(), "Failed BufferCollectionSync 1.\n");

  auto bind_result_2 =
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2));
  LOGRTN(bind_result_2.status(), "Failed BindSharedCollection 2.\n");

  zx::status collection_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  LOGRTN(collection_endpoints_3.status_value(), "Failed collection 3 channel create.\n");
  auto [collection_client_3, collection_server_3] = std::move(*collection_endpoints_3);
  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection_3(
      std::move(collection_client_3));
  LOGRTNC(token_client_3.channel().get() == ZX_HANDLE_INVALID, "Invalid token client 3.\n");

  auto bind_result_3 =
      allocator_2->BindSharedCollection(std::move(token_client_3), std::move(collection_server_3));
  LOGRTN(bind_result_3.status(), "Failed BindSharedCollection 2 -> 3.\n");

  fuchsia_sysmem::wire::BufferCollectionConstraints empty_constraints;

  auto set_constraints_3_result = collection_3->SetConstraints(false, std::move(empty_constraints));
  LOGRTN(set_constraints_3_result.status(), "Failed BufferCollectionSetConstraints 3 -> empty.\n");

  // Not all constraints have been input, so the buffers haven't been
  // allocated yet.
  auto check_result_1_fail = collection_1->CheckBuffersAllocated();
  LOGRTN(check_result_1_fail.status(), "Failed BufferCollectionCheckBuffersAllocated 1.\n");
  LOGRTNC(check_result_1_fail->status != ZX_OK,
          "BufferCollection allocated when shouldn't be. 1\n");
  auto check_result_2_fail = collection_2->CheckBuffersAllocated();
  LOGRTN(check_result_2_fail.status(), "Failed BufferCollectionCheckBuffersAllocated 2.\n");
  LOGRTNC(check_result_2_fail->status != ZX_OK,
          "BufferCollection allocated when shouldn't be. 2\n");

  auto set_constraints_2_result = collection_2->SetConstraints(true, std::move(constraints_2));
  LOGRTN(set_constraints_2_result.status(), "Failed BufferCollectionSetConstraints 2.\n");

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //
  auto allocate_result = collection_1->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  LOGRTN(allocate_result.status(), "WaitForBuffersAllocated, collection 1 failed.\n");
  LOGRTN(allocate_result->status,
         "WaitForBuffersAllocated, allocation_status collection 1 failed.\n");

  auto check_result_1 = collection_1->CheckBuffersAllocated();
  LOGRTN(check_result_1.status(), "CheckBuffersAllocated, collection 1 failed.\n");
  LOGRTN(check_result_1->status, "CheckBuffersAllocated, check_status collection 1 failed.\n");

  auto check_result_2 = collection_2->CheckBuffersAllocated();
  LOGRTN(check_result_2.status(), "CheckBuffersAllocated, collection 2 failed.\n");
  LOGRTN(check_result_2->status, "CheckBuffersAllocated, check_status collection 2 failed.\n");

  auto allocate_result_2 = collection_2->WaitForBuffersAllocated();
  LOGRTN(allocate_result_2.status(), "WaitForBuffersAllocated, collection 2 failed.\n");
  LOGRTN(allocate_result_2->status,
         "WaitForBuffersAllocated, allocation_status collection 2 failed.\n");

  auto allocate_result_3 = collection_3->WaitForBuffersAllocated();
  LOGRTN(allocate_result_3.status(), "WaitForBuffersAllocated, collection 3 failed.\n");
  LOGRTN(allocate_result_3->status,
         "WaitForBuffersAllocated, allocation_status collection 3 failed.\n");

  // Close to ensure grabbing null constraints from a closed collection
  // doesn't crash
  auto close_result = collection_3->Close();
  LOGRTN(close_result.status(), "Failed to close collection_client_3.\n");

  return 0;
}
