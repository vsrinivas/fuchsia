// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_RESOURCE_H_
#define SYSROOT_ZIRCON_SYSCALLS_RESOURCE_H_

#include <stdint.h>
#include <zircon/compiler.h>

// Resources that require a region allocator to handle exclusive reservations
// are defined in a contiguous block starting at 0 up to ZX_RSRC_KIND_COUNT-1.
// After that point, all resource 'kinds' are abstract and need no underlying
// bookkeeping. It's important that ZX_RSRC_KIND_COUNT is defined for each
// architecture to properly allocate only the bookkeeping necessary.
//
// TODO(fxbug.dev/32272): Don't expose ZX_RSRC_KIND_COUNT to userspace

typedef uint32_t zx_rsrc_kind_t;
#define ZX_RSRC_KIND_MMIO ((zx_rsrc_kind_t)0u)
#define ZX_RSRC_KIND_IRQ ((zx_rsrc_kind_t)1u)
#define ZX_RSRC_KIND_IOPORT ((zx_rsrc_kind_t)2u)
#define ZX_RSRC_KIND_HYPERVISOR ((zx_rsrc_kind_t)3u)
#define ZX_RSRC_KIND_ROOT ((zx_rsrc_kind_t)4u)
#define ZX_RSRC_KIND_VMEX ((zx_rsrc_kind_t)5u)
#define ZX_RSRC_KIND_SMC ((zx_rsrc_kind_t)6u)
#define ZX_RSRC_KIND_COUNT ((zx_rsrc_kind_t)7u)

typedef uint32_t zx_rsrc_flags_t;
#define ZX_RSRC_FLAG_EXCLUSIVE ((zx_rsrc_flags_t)0x00010000u)
#define ZX_RSRC_FLAGS_MASK ((zx_rsrc_flags_t)ZX_RSRC_FLAG_EXCLUSIVE)

#define ZX_RSRC_EXTRACT_KIND(x) ((x)&0x0000FFFF)
#define ZX_RSRC_EXTRACT_FLAGS(x) ((x)&0xFFFF0000)

#endif  // SYSROOT_ZIRCON_SYSCALLS_RESOURCE_H_
