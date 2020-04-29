// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"

#include <lib/async-testing/test_loop.h>
#include <lib/fdio/directory.h>

#include <thread>

#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"

namespace flatland {

using NullRendererTest = RendererTest;

// Make sure a valid token can be used to register a buffer collection. Make
// sure also that multiple calls to register buffer collection return
// different values for the GlobalBufferCollectionId.
TEST_F(NullRendererTest, RegisterCollectionTest) {
  auto tokens = CreateSysmemTokens(sysmem_allocator_.get());
  auto tokens2 = CreateSysmemTokens(sysmem_allocator_.get());

  NullRenderer renderer;

  // First id should be valid.
  auto bcid =
      renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(tokens.local_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // Second id should be valid.
  auto bcid2 =
      renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(tokens2.local_token));
  EXPECT_NE(bcid2, Renderer::kInvalidId);

  // Ids should not equal eachother.
  EXPECT_NE(bcid, bcid2);
}

// Multiple clients may need to reference the same buffer collection in the renderer
// (for example if they both need access to a global camera feed). In this case, both
// clients will be passing their own duped tokens to the same collection to the renderer,
// and will each get back a different ID. The collection itself (which is just a pointer)
// will be in the renderer's map twice. So if all tokens are set, both server-side
// registered collections should be allocated (since they are just pointers that refer
// to the same collection).
TEST_F(NullRendererTest, SameTokenTwiceTest) {
  NullRenderer renderer;

  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  // Create a client token to represent a single client.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr client_token;
  auto status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                              client_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  // First id should be valid.
  auto bcid =
      renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(tokens.local_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // Second id should be valid.
  auto bcid2 =
      renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_NE(bcid2, Renderer::kInvalidId);

  // Ids should not equal eachother.
  EXPECT_NE(bcid, bcid2);

  // Set the client constraints.
  SetClientConstraintsAndWaitForAllocated(sysmem_allocator_.get(), std::move(client_token));

  // Now check that both server ids are allocated.
  auto result_1 = renderer.Validate(bcid);
  auto result_2 = renderer.Validate(bcid2);

  EXPECT_NE(result_1, std::nullopt);
  EXPECT_NE(result_2, std::nullopt);

  // There should be 1 vmo.
  auto buffer_meta_data = *result_1;
  EXPECT_EQ(buffer_meta_data.vmo_count, 1U);

  buffer_meta_data = *result_2;
  EXPECT_EQ(buffer_meta_data.vmo_count, 1U);
}

// Make sure a bad token returns Renderer::kInvalidId. A "bad token" here can
// either be a null token, or a token that's a valid channel but just not a
// valid buffer collection token.
TEST_F(NullRendererTest, BadTokenTest) {
  NullRenderer renderer;

  // Null token should fail.
  auto bcid = renderer.RegisterBufferCollection(sysmem_allocator_.get(), nullptr);
  EXPECT_EQ(bcid, Renderer::kInvalidId);

  // A valid channel that isn't a buffer collection should also fail.
  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);
  flatland::BufferCollectionHandle handle{std::move(remote_endpoint)};
  ASSERT_TRUE(handle.is_valid());
  bcid = renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(handle));
  EXPECT_EQ(bcid, Renderer::kInvalidId);
}

// Test the Validate() function. First call Validate() without setting the client
// constraints, which should return std::nullopt, and then set the client constraints which
// should cause Validate() to return a valid BufferCollectionMetadata struct.
TEST_F(NullRendererTest, ValidationTest) {
  NullRenderer renderer;

  auto tokens = CreateSysmemTokens(sysmem_allocator_.get());

  auto bcid =
      renderer.RegisterBufferCollection(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_NE(bcid, Renderer::kInvalidId);

  // The buffer collection should not be valid here.
  EXPECT_EQ(renderer.Validate(bcid), std::nullopt);

  SetClientConstraintsAndWaitForAllocated(sysmem_allocator_.get(), std::move(tokens.local_token));

  // The buffer collection *should* be valid here.
  auto result = renderer.Validate(bcid);
  EXPECT_NE(result, std::nullopt);

  // There should be 1 vmo.
  auto buffer_meta_data = *result;
  EXPECT_EQ(buffer_meta_data.vmo_count, 1U);
}

// Test to make sure we can call RegisterBufferCollection() and Validate()
// simultaneously from multiple threads and have it work.
TEST_F(NullRendererTest, MultithreadingTest) {
  NullRenderer renderer;

  const uint32_t kNumThreads = 50;

  auto register_and_validate_function = [&renderer]() {
    // Make a test loop.
    async::TestLoop loop;

    // Make an extra sysmem allocator for tokens.
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());

    auto tokens = CreateSysmemTokens(sysmem_allocator.get());
    auto bcid =
        renderer.RegisterBufferCollection(sysmem_allocator.get(), std::move(tokens.local_token));
    EXPECT_NE(bcid, Renderer::kInvalidId);

    SetClientConstraintsAndWaitForAllocated(sysmem_allocator.get(), std::move(tokens.dup_token));

    // The buffer collection *should* be valid here.
    auto result = renderer.Validate(bcid);
    EXPECT_NE(result, std::nullopt);

    // There should be 1 vmo.
    auto buffer_meta_data = *result;
    EXPECT_EQ(buffer_meta_data.vmo_count, 1U);

    loop.RunUntilIdle();
  };

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread(register_and_validate_function));
  }

  for (auto&& thread : threads) {
    thread.join();
  }

  // Validate the ids here one more time to make sure the renderer's internal
  // state hasn't been corrupted. The ids are generated incrementally so we
  // know that we should have id values in the range [1, kNumThreads].
  for (GlobalBufferCollectionId i = 1; i <= kNumThreads; i++) {
    // The buffer collection *should* be valid here.
    auto result = renderer.Validate(i);
    EXPECT_NE(result, std::nullopt);

    // There should be 1 vmo.
    auto buffer_meta_data = *result;
    EXPECT_EQ(buffer_meta_data.vmo_count, 1U);
  }

  // Check that if we give a non-valid bcid, that the result is null.
  auto result = renderer.Validate(kNumThreads + 1);
  EXPECT_EQ(result, std::nullopt);
}

}  // namespace flatland
