// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"

namespace flatland {

// Common testing base class to be used across different unittests that
// require Vulkan and a SysmemAllocator.
class RendererTest : public escher::test::TestWithVkValidationLayer {
 protected:
  void SetUp() override {
    TestWithVkValidationLayer::SetUp();
    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    sysmem_allocator_->SetDebugClientInfo(fsl::GetCurrentProcessName() + " RendererTest",
                                          fsl::GetCurrentProcessKoid());
  }

  void TearDown() override {
    sysmem_allocator_ = nullptr;
    TestWithVkValidationLayer::TearDown();
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_TESTS_COMMON_H_
