// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PAGETABLE_H
#define PAGETABLE_H

#include "types.h"
#include <limits.h> // PAGE_SIZE

#ifndef PAGE_SHIFT

#if PAGE_SIZE == 4096
#define PAGE_SHIFT 12
#else
#error PAGE_SHIFT not defined
#endif

#endif

#define PAGE_PRESENT (1 << 0)
#define PAGE_RW (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_PWT (1 << 3)
#define PAGE_PCD (1 << 4)
#define PAGE_PAT (1 << 7)

#endif // PAGETABLE_H
