// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/scpi.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <threads.h>

#define GPU_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define GPU_INFO(fmt, ...)  zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PWR_KEY         0x14
#define PWR_OVERRIDE1   0x16

#define READ32_GPU_REG(offset)          readl(io_buffer_virt(&gpu->gpu_buffer) \
                                        + offset*4)
#define WRITE32_GPU_REG(offset, value)  writel(value, io_buffer_virt(&gpu->gpu_buffer) \
                                        + offset*4)

#define READ32_HIU_REG(offset)          readl(io_buffer_virt(&gpu->hiu_buffer) \
                                        + offset*4)
#define WRITE32_HIU_REG(offset, value)  writel(value, io_buffer_virt(&gpu->hiu_buffer) \
                                        + offset*4)

#define READ32_PRESET_REG(offset)          readl(io_buffer_virt(&gpu->preset_buffer) \
                                           + offset*4)
#define WRITE32_PRESET_REG(offset, value)  writel(value, io_buffer_virt(&gpu->preset_buffer) \
                                           + offset*4)

#define CLK_ENABLED_BIT_SHIFT             8
#define CALCULATE_CLOCK_MUX(enabled, base, divisor) \
        ((!!(enabled) << CLK_ENABLED_BIT_SHIFT) | (base << 9) | (divisor - 1))

#define CLOCK_MUX_MASK                  0xFFF

#define MAX_GPU_CLK_FREQ                5
#define FINAL_MUX_BIT_SHIFT             31

enum {
    MMIO_GPU,
    MMIO_HIU,
    MMIO_PRESET,
};

typedef struct {
    uint32_t reset0_level_offset;
    uint32_t reset0_mask_offset;
    uint32_t reset2_level_offset;
    uint32_t reset2_mask_offset;
    uint32_t hhi_clock_cntl_offset;
    uint32_t gpu_clk_freq[MAX_GPU_CLK_FREQ];
}aml_gpu_block_t;

typedef struct {
    platform_device_protocol_t  pdev;

    zx_device_t*                zxdev;

    io_buffer_t                 hiu_buffer;
    io_buffer_t                 preset_buffer;
    io_buffer_t                 gpu_buffer;

    aml_gpu_block_t*            gpu_block;
} aml_gpu_t;
