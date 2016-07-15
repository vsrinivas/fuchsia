// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>

#define REG8(addr) ((volatile uint8_t *)(uintptr_t)(addr))
#define REG16(addr) ((volatile uint16_t *)(uintptr_t)(addr))
#define REG32(addr) ((volatile uint32_t *)(uintptr_t)(addr))
#define REG64(addr) ((volatile uint64_t *)(uintptr_t)(addr))

#define writeb(v, a) (*REG8(a) = (v))
#define readb(a) (*REG8(a))
#define writew(v, a) (*REG16(a) = (v))
#define readw(a) (*REG16(a))
#define writel(v, a) (*REG32(a) = (v))
#define readl(a) (*REG32(a))
#define writell(v, a) (*REG64(a) = (v))
#define readll(a) (*REG64(a))
