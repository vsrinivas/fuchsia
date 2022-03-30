// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <optional>

#include <zxtest/zxtest.h>

namespace {

zx::vmo CreateVmoWithPolicy(size_t size, std::optional<uint32_t> cache_policy) {
  zx::vmo vmo = {};
  zx_status_t status = zx::vmo::create(size, /*options=*/0, &vmo);
  ZX_ASSERT_MSG((status == ZX_OK), "creating vmo failed: %s", zx_status_get_string(status));
  if (cache_policy.has_value()) {
    status = vmo.set_cache_policy(*cache_policy);
    ZX_ASSERT_MSG((status == ZX_OK), "setting vmo cache policy failed: %s",
                  zx_status_get_string(status));
  }
  return vmo;
}

zx::vmo CreateVmo(size_t size) { return CreateVmoWithPolicy(size, std::nullopt); }

zx::vmo DuplicateVmo(const zx::vmo& vmo) {
  zx::vmo out_vmo = {};
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &out_vmo);
  ZX_ASSERT_MSG((status == ZX_OK), "duplicating vmo failed: %s", zx_status_get_string(status));
  return out_vmo;
}

TEST(MmioBuffer, CInit) {
  const size_t vmo_sz = zx_system_get_page_size();
  zx::vmo vmo = CreateVmo(vmo_sz);
  mmio_buffer_t mb = {};

  // |buffer| is invalid.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, mmio_buffer_init(nullptr, 0, vmo_sz, DuplicateVmo(vmo).get(),
                                                  ZX_CACHE_POLICY_UNCACHED));
  // |offset is invalid.
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            mmio_buffer_init(&mb, -1, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, mmio_buffer_init(&mb, vmo_sz + 1, vmo_sz, DuplicateVmo(vmo).get(),
                                                  ZX_CACHE_POLICY_UNCACHED));
  // |size| is invalid
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            mmio_buffer_init(&mb, 0, 0, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, mmio_buffer_init(&mb, 0, vmo_sz + 1, DuplicateVmo(vmo).get(),
                                                  ZX_CACHE_POLICY_UNCACHED));
  // |size| + |offset| are collectively invalid.
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            mmio_buffer_init(&mb, vmo_sz + 1 / 2, vmo_sz / 2, DuplicateVmo(vmo).get(),
                             ZX_CACHE_POLICY_UNCACHED));

  // |vmo| is invalid
  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            mmio_buffer_init(&mb, 0, vmo_sz, ZX_HANDLE_INVALID, ZX_CACHE_POLICY_UNCACHED));
  // |cache_policy| is invalid.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            mmio_buffer_init(&mb, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_MASK + 1));

  ASSERT_OK(mmio_buffer_init(&mb, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  mmio_buffer_release(&mb);
  ASSERT_EQ(mb.vmo, ZX_HANDLE_INVALID);
}

TEST(MmioBuffer, CppLifecycle) {
  const size_t vmo_sz = zx_system_get_page_size();
  zx::vmo vmo = CreateVmo(vmo_sz);
  MMIO_PTR volatile uint8_t* ptr = nullptr;
  {
    std::optional<fdf::MmioBuffer> mmio = {};
    ASSERT_OK(
        fdf::MmioBuffer::Create(0, vmo_sz, std::move(vmo), ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
    ptr = reinterpret_cast<MMIO_PTR volatile uint8_t*>(mmio->get());
    // This write should succeed.
    MmioWrite8(0xA5, ptr);
  }

  // This should fault because the dtor of MmioBuffer unmapped the range.
  ASSERT_DEATH(([&ptr]() { MmioWrite8(0xA5, ptr); }));
}

TEST(MmioBuffer, AlreadyMapped) {
  const size_t vmo_sz = zx_system_get_page_size();
  zx::vmo vmo = CreateVmo(vmo_sz);
  mmio_buffer_t mb1 = {};
  mmio_buffer_t mb2 = {};

  ASSERT_OK(mmio_buffer_init(&mb1, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  // A second mapping with a different cache policy should fail.
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            mmio_buffer_init(&mb2, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_CACHED));
  // The same cache policy should be fine in a second mmio_buffer.
  ASSERT_EQ(ZX_OK,
            mmio_buffer_init(&mb2, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  mmio_buffer_release(&mb1);
  mmio_buffer_release(&mb2);
}

TEST(MmioBuffer, AlreadySetVmoCachePolicy) {
  const size_t vmo_sz = zx_system_get_page_size();
  uint32_t policy = ZX_CACHE_POLICY_UNCACHED_DEVICE;
  zx::vmo vmo = CreateVmoWithPolicy(vmo_sz, policy);
  mmio_buffer_t mb1 = {};
  mmio_buffer_t mb2 = {};

  // Since the VMO isn't mapped yet the mmio_buffer_t policy can differ.
  ASSERT_OK(mmio_buffer_init(&mb1, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  // Trying to map with another policy will fail.
  ASSERT_EQ(ZX_ERR_BAD_STATE, mmio_buffer_init(&mb2, 0, vmo_sz, DuplicateVmo(vmo).get(), policy));
  // A second mmio_buffer_t with the existing policy will succeed.
  ASSERT_OK(mmio_buffer_init(&mb2, 0, vmo_sz, DuplicateVmo(vmo).get(), ZX_CACHE_POLICY_UNCACHED));
  mmio_buffer_release(&mb1);
  mmio_buffer_release(&mb2);
}

}  // namespace
