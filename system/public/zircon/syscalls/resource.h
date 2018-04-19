// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

// Resources that require a region allocator to handle exclusive reservations
// are defined in a contiguous block starting at 0 up to ZX_RSRC_STATIC_COUNT-1.
// After that point, all resource 'kinds' are abstract and need no underlying
// bookkeeping. It's important that ZX_RSRC_STATIC_COUNT is defined for each
// architecture to properly allocate only the bookkeeping necessary.
//
// TODO(ZX-2419): Give these a type and don't expose ZX_RSRC_STATIC_COUNT to userspace

#define ZX_RSRC_KIND_MMIO           0u
#define ZX_RSRC_KIND_IRQ            1u
#define ZX_RSRC_KIND_IOPORT         2u
#define ZX_RSRC_KIND_HYPERVISOR     3u
#define ZX_RSRC_KIND_ROOT           4u
#define ZX_RSRC_KIND_COUNT          5u

#define ZX_RSRC_FLAG_EXCLUSIVE      0x00010000u
#define ZX_RSRC_FLAGS_MASK          (ZX_RSRC_FLAG_EXCLUSIVE)

#define ZX_RSRC_EXTRACT_KIND(x)     ((x) & 0x0000FFFF)
#define ZX_RSRC_EXTRACT_FLAGS(x)    ((x) & 0xFFFF0000)
