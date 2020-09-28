// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "asan_impl.h"
#include "libc.h"
#include "zircon_impl.h"

#define ASAN_SHADOW_SHIFT 3

#define SHADOW_VMO_NAME "asan-shadow"

#if __has_feature(address_sanitizer)

static sanitizer_shadow_bounds_t shadow_bounds ATTR_RELRO;
static zx_handle_t shadow_vmo ATTR_RELRO;

__NO_SAFESTACK NO_ASAN void __asan_early_init(void) {
  zx_info_vmar_t info;
  zx_status_t status =
      _zx_object_get_info(__zircon_vmar_root_self, ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK)
    __builtin_trap();

  // Find the top of the accessible address space.
  uintptr_t top = info.base + info.len;

  // Round it up to a power-of-two size.  There may be some pages at
  // the top that can't actually be mapped, but for purposes of the
  // the shadow, we'll pretend they could be.
  int bit = (sizeof(uintptr_t) * CHAR_BIT) - __builtin_clzl(top);
  if (top != (uintptr_t)1 << bit)
    top = (uintptr_t)1 << (bit + 1);

  // The shadow is a fraction of the address space at the bottom.
  size_t shadow_virtual_size = top >> ASAN_SHADOW_SHIFT;

  // The shadow of the shadow is never used, so it'll be left unmapped.
  size_t shadow_shadow_size = shadow_virtual_size >> ASAN_SHADOW_SHIFT;

  // The VMAR reserved for the shadow covers the region from the
  // lowest permitted mapping address (info.base) up to the notional
  // top of the shadow (shadow_virtual_size).
  zx_handle_t shadow_vmar;
  uintptr_t shadow_addr;
  status = _zx_vmar_allocate(
      __zircon_vmar_root_self,
      ZX_VM_SPECIFIC | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
      shadow_virtual_size - info.base, &shadow_vmar, &shadow_addr);
  if (status != ZX_OK || shadow_addr != info.base)
    __builtin_trap();

  // The actual shadow that needs to be mapped starts at the top of
  // the shadow of the shadow, and has a page of shadow for each
  // (1<<ASAN_SHADOW_SHIFT) pages that can actually be mapped.
  size_t shadow_used_size =
      ((((info.base + info.len) >> ASAN_SHADOW_SHIFT) + PAGE_SIZE - 1) & -PAGE_SIZE) -
      shadow_shadow_size;

  // Now we're ready to allocate and map the actual shadow. We keep the VMO
  // to allow decommit of shadow memory later, see below.
  status = _zx_vmo_create(shadow_used_size, 0, &shadow_vmo);
  if (status != ZX_OK)
    __builtin_trap();
  _zx_object_set_property(shadow_vmo, ZX_PROP_NAME, SHADOW_VMO_NAME, sizeof(SHADOW_VMO_NAME) - 1);

  status =
      _zx_vmar_map(shadow_vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                   shadow_shadow_size - info.base, shadow_vmo, 0, shadow_used_size, &shadow_addr);
  if (status != ZX_OK || shadow_addr != shadow_shadow_size)
    __builtin_trap();

  // Drop the VMAR handle.
  // The mappings in the shadow region can never be changed.
  status = _zx_handle_close(shadow_vmar);
  if (status != ZX_OK)
    __builtin_trap();

  // Store the values to be exported to the sanitizer runtime library.
  shadow_bounds.shadow_base = shadow_shadow_size;
  shadow_bounds.shadow_limit = shadow_virtual_size;
  shadow_bounds.memory_limit = top;

  // There's nothing here that the compiler should think it could move
  // around much, so this almost certainly doesn't actually do anything.
  // But the notion is that after this point, it's OK to run ASanified
  // functions whereas before now it wasn't.  So doing this expresses
  // explicitly the intent that everything before here must be well and
  // truly done before anything after here is safe to run.
  atomic_signal_fence(memory_order_seq_cst);
}

__EXPORT
sanitizer_shadow_bounds_t __sanitizer_shadow_bounds(void) { return shadow_bounds; }

NO_ASAN static void decommit_if_zero(uintptr_t page) {
  const uint64_t *ptr = (uint64_t *)page;
  for (int i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
    if (ptr[i] != 0)
      return;
  }

  zx_status_t status = _zx_vmo_op_range(shadow_vmo, ZX_VMO_OP_DECOMMIT,
                                        page - shadow_bounds.shadow_base, PAGE_SIZE, NULL, 0);
  if (status != ZX_OK) {
    __builtin_trap();
  }
}

__EXPORT
NO_ASAN void __sanitizer_fill_shadow(uintptr_t base, size_t size, uint8_t value, size_t threshold) {
  const uintptr_t shadow_base = base >> ASAN_SHADOW_SHIFT;
  if (shadow_base < shadow_bounds.shadow_base) {
    __builtin_trap();
  }
  const size_t shadow_size = size >> ASAN_SHADOW_SHIFT;
  if (!value && shadow_size >= threshold && shadow_size >= PAGE_SIZE) {
    // TODO(fxbug.dev/41009): Handle shadow_size < PAGE_SIZE.
    uintptr_t page_start = (shadow_base + PAGE_SIZE - 1) & -PAGE_SIZE;
    uintptr_t page_end = (shadow_base + shadow_size) & -PAGE_SIZE;
    // Memset the partial pages, and decommit them if they are zero-pages.
    if (page_start - shadow_base > 0) {
      __unsanitized_memset((void *)shadow_base, 0, page_start - shadow_base);
      decommit_if_zero(page_start - PAGE_SIZE);
    }

    if (shadow_base + shadow_size - page_end > 0) {
      __unsanitized_memset((void *)page_end, 0, shadow_base + shadow_size - page_end);
      decommit_if_zero(page_end);
    }

    // Decommit the whole pages always, so the next time we use them we will get
    // fresh zero-pages.
    zx_status_t status =
        _zx_vmo_op_range(shadow_vmo, ZX_VMO_OP_DECOMMIT, page_start - shadow_bounds.shadow_base,
                         page_end - page_start, NULL, 0);
    if (status != ZX_OK) {
      __builtin_trap();
    }
  } else {
    __unsanitized_memset((void *)shadow_base, value, shadow_size);
  }
}

#else

static const char kBadDepsMessage[] =
    "module compiled with -fsanitize=address loaded in process without it";

// This should never be called in the unsanitized runtime.
// But it's still part of the ABI.
__EXPORT
sanitizer_shadow_bounds_t __sanitizer_shadow_bounds(void) {
  __sanitizer_log_write(kBadDepsMessage, sizeof(kBadDepsMessage) - 1);
  __builtin_trap();
}

__EXPORT
void __sanitizer_fill_shadow(uintptr_t base, size_t size, uint8_t value, uintptr_t threshold) {
  __sanitizer_log_write(kBadDepsMessage, sizeof(kBadDepsMessage) - 1);
  __builtin_trap();
}

#endif  // __has_feature(address_sanitizer)
