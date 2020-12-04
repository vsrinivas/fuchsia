// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"

namespace flatland {

struct SysmemTokens {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
};

static inline SysmemTokens CreateSysmemTokens(fuchsia::sysmem::Allocator_Sync* sysmem_allocator) {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);
  return {std::move(local_token), std::move(dup_token)};
}

// Common testing base class to be used across different unittests that
// require Vulkan and a SysmemAllocator.
class RendererTest : public escher::test::TestWithVkValidationLayer {
 protected:
  void SetUp() override {
    TestWithVkValidationLayer::SetUp();
    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
  }

  void TearDown() override {
    sysmem_allocator_ = nullptr;
    TestWithVkValidationLayer::TearDown();
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
