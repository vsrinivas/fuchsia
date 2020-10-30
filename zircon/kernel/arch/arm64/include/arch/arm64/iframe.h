// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_IFRAME_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_IFRAME_H_

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <zircon/compiler.h>

// exception handling
// This is the main struct used by architecture-independent code.
struct iframe_t {
  uint64_t r[30];
  uint64_t lr;
  uint64_t usp;
  uint64_t elr;
  uint64_t spsr;
  uint64_t mdscr;
  uint64_t pad2[1];  // Keep structure multiple of 16-bytes for stack alignment.
};

static_assert(sizeof(iframe_t) % 16u == 0u);

#endif  // !__ASSEMBLER__

#define ARM64_IFRAME_OFFSET_R 0
#define ARM64_IFRAME_OFFSET_LR (30 * 8)
#define ARM64_IFRAME_OFFSET_USP (31 * 8)
#define ARM64_IFRAME_OFFSET_ELR (32 * 8)
#define ARM64_IFRAME_OFFSET_SPSR (33 * 8)
#define ARM64_IFRAME_OFFSET_MDSCR (34 * 8)

#ifndef __ASSEMBLER__

static_assert(__offsetof(iframe_t, r[0]) == ARM64_IFRAME_OFFSET_R, "");
static_assert(__offsetof(iframe_t, lr) == ARM64_IFRAME_OFFSET_LR, "");
static_assert(__offsetof(iframe_t, usp) == ARM64_IFRAME_OFFSET_USP, "");
static_assert(__offsetof(iframe_t, elr) == ARM64_IFRAME_OFFSET_ELR, "");
static_assert(__offsetof(iframe_t, spsr) == ARM64_IFRAME_OFFSET_SPSR, "");
static_assert(__offsetof(iframe_t, mdscr) == ARM64_IFRAME_OFFSET_MDSCR, "");

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_IFRAME_H_
