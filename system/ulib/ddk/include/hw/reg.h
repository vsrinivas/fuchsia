// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#define REG8(addr) ((volatile uint8_t *)(uintptr_t)(addr))
#define REG16(addr) ((volatile uint16_t *)(uintptr_t)(addr))
#define REG32(addr) ((volatile uint32_t *)(uintptr_t)(addr))
#define REG64(addr) ((volatile uint64_t *)(uintptr_t)(addr))

#define writeb(v, a) (*REG8(a) = (v))
#define readb(a) (*REG8(a))
#define set_bitsb(v, a) (*REG8(a) |= (v))
#define clr_bitsb(v, a) (*REG8(a) &= (~(v)))

#define writew(v, a) (*REG16(a) = (v))
#define readw(a) (*REG16(a))
#define set_bitsw(v, a) (*REG16(a) |= (v))
#define clr_bitsw(v, a) (*REG16(a) &= (~(v)))

#define writel(v, a) (*REG32(a) = (v))
#define readl(a) (*REG32(a))
#define set_bitsl(v, a) (*REG32(a) |= (v))
#define clr_bitsl(v, a) (*REG32(a) &= (~(v)))

#define writell(v, a) (*REG64(a) = (v))
#define readll(a) (*REG64(a))
#define set_bitsll(v, a) (*REG64(a) |= (v))
#define clr_bitsll(v, a) (*REG64(a) &= (~(v)))
