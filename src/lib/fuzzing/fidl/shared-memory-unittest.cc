// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shared-memory.h"

#include <zircon/errors.h>

#include <gtest/gtest.h>

namespace fuzzing {

TEST(SharedMemoryTest, Create) {
  SharedMemory shmem;

  // Bad length
  EXPECT_EQ(shmem.Create(0), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), 0u);

  // Valid
  size_t size = 0x1000;
  EXPECT_EQ(shmem.Create(size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);

  size_t actual;
  EXPECT_EQ(shmem.vmo().get_size(&actual), ZX_OK);
  EXPECT_EQ(size, actual);

  // Can recreate
  size *= 2;
  EXPECT_EQ(shmem.Create(size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);
}

TEST(SharedMemoryTest, Share) {
  SharedMemory shmem;

  // Not created
  zx::vmo vmo;
  EXPECT_EQ(shmem.Share(&vmo), ZX_ERR_BAD_HANDLE);

  // Bad pointer
  size_t size = 0x1000;
  EXPECT_EQ(shmem.Create(size), ZX_OK);
  EXPECT_EQ(shmem.Share(nullptr), ZX_ERR_INVALID_ARGS);

  // Valid
  EXPECT_EQ(shmem.Share(&vmo), ZX_OK);

  size_t actual;
  EXPECT_EQ(vmo.get_size(&actual), ZX_OK);
  EXPECT_EQ(size, actual);
}

TEST(SharedMemoryTest, Link) {
  SharedMemory shmem;

  // Bad VMO.
  zx::vmo vmo;
  size_t size = 0x1000;
  EXPECT_EQ(shmem.Link(vmo, size), ZX_ERR_BAD_HANDLE);
  EXPECT_EQ(shmem.addr(), 0u);

  // Bad length.
  EXPECT_EQ(zx::vmo::create(size, 0, &vmo), ZX_OK);
  EXPECT_EQ(shmem.Link(vmo, size + 1), ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(shmem.addr(), 0u);

  // Valid
  EXPECT_EQ(shmem.Link(vmo, size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);

  // Can remap.
  size *= 2;
  EXPECT_EQ(zx::vmo::create(size, 0, &vmo), ZX_OK);
  EXPECT_EQ(shmem.Link(vmo, size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);
}

TEST(SharedMemoryTest, Reset) {
  SharedMemory shmem;

  // Valid even if unmapped
  shmem.Reset();

  // Valid
  size_t size = 0x1000;
  EXPECT_EQ(shmem.Create(size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);

  shmem.Reset();
  EXPECT_EQ(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), 0u);

  // Can map again after reset.
  EXPECT_EQ(shmem.Create(size), ZX_OK);
  EXPECT_NE(shmem.addr(), 0u);
  EXPECT_EQ(shmem.len(), size);
}

}  // namespace fuzzing
