// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/process.h>

#include <zxtest/zxtest.h>

namespace {

// Test that VMO handles support user signals
TEST(VmoSignalTestCase, SignalSanity) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_vmo_create(4096, 0, &vmo), "");
  ASSERT_NE(vmo, ZX_HANDLE_INVALID, "zx_vmo_create() failed");

  zx_signals_t out_signals = 0;

  // This is not timing dependent, if this fails is not a flake.
  ASSERT_EQ(zx_object_wait_one(vmo, ZX_USER_SIGNAL_0, zx_deadline_after(2), &out_signals),
            ZX_ERR_TIMED_OUT, "");

  ASSERT_EQ(out_signals, ZX_VMO_ZERO_CHILDREN, "unexpected initial signal set");
  ASSERT_OK(zx_object_signal(vmo, 0, ZX_USER_SIGNAL_0), "");
  ASSERT_OK(zx_object_wait_one(vmo, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, &out_signals), "");
  ASSERT_EQ(out_signals, ZX_USER_SIGNAL_0 | ZX_VMO_ZERO_CHILDREN,
            "ZX_USER_SIGNAL_0 not set after successful wait");

  ASSERT_OK(zx_handle_close(vmo), "");
}

zx_status_t VmoHasNoChildren(zx_handle_t vmo) {
  zx_signals_t signals;
  return zx_object_wait_one(vmo, ZX_VMO_ZERO_CHILDREN, ZX_TIME_INFINITE, &signals);
}

zx_status_t VmoHasChildren(zx_handle_t vmo) {
  zx_signals_t signals;
  zx_status_t res = zx_object_wait_one(vmo, ZX_VMO_ZERO_CHILDREN, zx_deadline_after(2), &signals);
  return (res == ZX_ERR_TIMED_OUT) ? ZX_OK : res;
}

TEST(VmoSignalTestCase, ChildSignalClone) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_vmo_create(4096u * 2, 0, &vmo), "");
  ASSERT_NE(vmo, ZX_HANDLE_INVALID, "");

  zx_handle_t clone = ZX_HANDLE_INVALID;
  zx_handle_t clone2 = ZX_HANDLE_INVALID;

  // The waits below with timeout are not timing dependent, if this fails is not a flake.

  for (int ix = 0; ix != 10; ++ix) {
    ASSERT_OK(VmoHasNoChildren(vmo), "");

    ASSERT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0u, 4096u, &clone), "");

    ASSERT_OK(VmoHasNoChildren(clone), "");
    ASSERT_OK(VmoHasChildren(vmo), "");

    ASSERT_OK(zx_vmo_create_child(clone, ZX_VMO_CHILD_COPY_ON_WRITE, 0u, 4096u, &clone2), "");

    ASSERT_OK(VmoHasNoChildren(clone2), "");
    ASSERT_OK(VmoHasChildren(clone), "");
    ASSERT_OK(VmoHasChildren(vmo), "");

    ASSERT_OK(zx_handle_close(clone), "");
    ASSERT_OK(VmoHasChildren(vmo), "");
    ASSERT_OK(VmoHasNoChildren(clone2), "");

    ASSERT_OK(zx_handle_close(clone2), "");
  }

  ASSERT_OK(zx_handle_close(vmo), "");
}

TEST(VmoSignalTestCase, ChildSignalMap) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_OK(zx_vmo_create(4096u * 2, 0, &vmo), "");
  ASSERT_NE(vmo, ZX_HANDLE_INVALID, "");

  zx_handle_t clone = ZX_HANDLE_INVALID;

  zx_vm_option_t options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

  for (int ix = 0; ix != 10; ++ix) {
    ASSERT_OK(VmoHasNoChildren(vmo), "");

    ASSERT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0u, 4096u, &clone), "");

    uintptr_t addr = 0;
    ASSERT_OK(zx_vmar_map(zx_vmar_root_self(), options, 0u, clone, 0, 4096u, &addr), "");

    ASSERT_OK(VmoHasChildren(vmo), "");

    ASSERT_OK(zx_handle_close(clone), "");

    ASSERT_OK(VmoHasChildren(vmo), "");

    ASSERT_OK(zx_vmar_unmap(zx_vmar_root_self(), addr, 4096u), "");
  }

  ASSERT_OK(zx_handle_close(vmo), "");
}

}  // namespace
