// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#define REG8(addr) ((volatile uint8_t *)(uintptr_t)(addr))
#define REG16(addr) ((volatile uint16_t *)(uintptr_t)(addr))
#define REG32(addr) ((volatile uint32_t *)(uintptr_t)(addr))
#define REG64(addr) ((volatile uint64_t *)(uintptr_t)(addr))

#define RMWREG8(addr, startbit, width, val) \
    writeb((readb(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG16(addr, startbit, width, val) \
    writew((readw(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG32(addr, startbit, width, val) \
    writel((readl(addr) & ~(((1 << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))
#define RMWREG64(addr, startbit, width, val) \
    writell((readll(addr) & ~(((1ull << (width)) - 1) << (startbit))) | ((val) << (startbit)), (addr))

#define writeb(v, a) (*REG8(a) = (v))
#define readb(a) (*REG8(a))
#define set_bitsb(v, a) writeb(readb(a) | (v), (a))
#define clr_bitsb(v, a) writeb(readb(a) & ~(v), (a))

#define writew(v, a) (*REG16(a) = (v))
#define readw(a) (*REG16(a))
#define set_bitsw(v, a) writew(readw(a) | (v), (a))
#define clr_bitsw(v, a) writew(readw(a) & ~(v), (a))

#define writel(v, a) (*REG32(a) = (v))
#define readl(a) (*REG32(a))
#define set_bitsl(v, a) writel(readl(a) | (v), (a))
#define clr_bitsl(v, a) writel(readl(a) & ~(v), (a))

#define writell(v, a) (*REG64(a) = (v))
#define readll(a) (*REG64(a))
#define set_bitsll(v, a) writell(readll(a) | (v), (a))
#define clr_bitsll(v, a) writell(readll(a) & ~(v), (a))
