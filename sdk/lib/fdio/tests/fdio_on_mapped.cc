// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <sys/mman.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

/// Helper function that returns a RAII object that wraps a pointer obtained by mmap so that it
/// is automatically unmapped (via `munmap`) when it goes out of scope. Will cause test failure
/// if the region could not be unmapped, but does not stop a test from continuing.
auto defer_munmap(void* addr, size_t size) {
  EXPECT_NE(addr, nullptr);
  EXPECT_GT(size, 0);
  return fit::defer([addr, size]() {
    EXPECT_EQ(munmap(addr, size), 0, "Could not unmap memory (ptr=%p, size=%zu): %s", addr, size,
              strerror(errno));
  });
}

TEST(OnMappedTest, OnMmapped) {
  static constexpr zxio_ops_t test_ops = []() {
    zxio_ops_t ops = zxio_default_ops;
    ops.vmo_get = [](zxio_t* io, zxio_vmo_flags_t flags, zx_handle_t* out_vmo) {
      return zx_vmo_create(PAGE_SIZE, 0, out_vmo);
    };
    ops.on_mapped = [](zxio_t* io, void* ptr) {
      uintptr_t* data = static_cast<uintptr_t*>(ptr);
      *data = reinterpret_cast<uintptr_t>(ptr);
      return ZX_OK;
    };
    return ops;
  }();

  auto open_test_fd = [] {
    zxio_storage_t* storage = nullptr;
    fdio_t* fdio = fdio_zxio_create(&storage);
    zxio_t* zxio = fdio_get_zxio(fdio);
    zxio_init(zxio, &test_ops);
    return fdio_bind_to_fd(fdio, -1, 0);
  };

  fbl::unique_fd test_fd(open_test_fd());
  ASSERT_TRUE(test_fd.is_valid());
  uintptr_t* ptr = static_cast<uintptr_t*>(
      mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, test_fd.get(), 0));
  ASSERT_NE(reinterpret_cast<intptr_t>(ptr), -1);
  auto cleanup = defer_munmap(ptr, PAGE_SIZE);
  EXPECT_EQ(*ptr, reinterpret_cast<uintptr_t>(ptr));
}

}  // namespace
