// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using MemoryTest = SessionTest;

// Creates a memory object and verifies that the allocation size validation
// logic is working.
TEST_F(MemoryTest, MemoryAllocationSizeValidation) {
  uint64_t vmo_size = 4096;
  zx::vmo vmo;

  // Create a vmo, and verify allocation size cannot be 0.
  zx_status_t status = zx::vmo::create(vmo_size, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  uint32_t memory_id = 1;
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(
      memory_id, std::move(vmo), 0, fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError(
      "Memory::New(): allocation_size argument (0) is not valid.");

  // Re-create a vmo, and verify allocation size cannot be greater than
  // vmo_size.
  status = zx::vmo::create(vmo_size, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_FALSE(Apply(
      scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), vmo_size + 1,
                                 fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError(
      "Memory::New(): allocation_size (4097) is larger than the size of the "
      "corresponding vmo (4096).");

  // Re-create a vmo, and verify allocation size can be < vmo_size.
  status = zx::vmo::create(vmo_size, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(
      memory_id, std::move(vmo), 1, fuchsia::images::MemoryType::HOST_MEMORY)));

  // Re-create a vmo, and verify allocation size can be == vmo_size.
  status = zx::vmo::create(vmo_size, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(
      scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), vmo_size,
                                 fuchsia::images::MemoryType::HOST_MEMORY)));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
