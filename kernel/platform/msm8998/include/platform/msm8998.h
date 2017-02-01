// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#define SDRAM_BASE 0x80000000

#define MSM8998_PERIPH_BASE_PHYS    (0x00000000U)
#define MSM8998_PERIPH_SIZE         (0x40000000U) // 1GB
#define MSM8998_PERIPH_BASE_VIRT    (0xffffffffc0000000UL)

#define MEMORY_APERTURE_SIZE        (30ULL * 1024 * 1024 * 1024)

#define MSM8998_GIC_BASE_PHYS       (MSM8998_PERIPH_BASE_PHYS + 0x17a00000)
#define MSM8998_GIC_BASE_VIRT       (MSM8998_PERIPH_BASE_VIRT + 0x17a00000)

/* interrupts */
#define ARM_GENERIC_TIMER_PHYSICAL_INT 30
#define UART_INT       (32 + 114)

#define MAX_INT 640
