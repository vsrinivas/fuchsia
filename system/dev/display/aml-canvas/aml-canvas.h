// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <hw/reg.h>
#include <threads.h>

#define NUM_CANVAS_ENTRIES 256
#define CANVAS_BYTE_STRIDE 32

#define IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b))-1)))

#define CANVAS_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define CANVAS_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define READ32_DMC_REG(a)                readl(io_buffer_virt(&canvas->dmc_regs) + a)
#define WRITE32_DMC_REG(a, v)            writel(v, io_buffer_virt(&canvas->dmc_regs) + a)

#define DMC_CAV_LUT_DATAL                   (0x12 << 2)
#define DMC_CAV_LUT_DATAH                   (0x13 << 2)
#define DMC_CAV_LUT_ADDR                    (0x14 << 2)

#define DMC_CAV_ADDR_LMASK                  0x1fffffff
#define DMC_CAV_WIDTH_LMASK                 0x7
#define DMC_CAV_WIDTH_LWID                  3
#define DMC_CAV_WIDTH_LBIT                  29

#define DMC_CAV_WIDTH_HMASK                 0x1ff
#define DMC_CAV_WIDTH_HBIT                  0
#define DMC_CAV_HEIGHT_MASK                 0x1fff
#define DMC_CAV_HEIGHT_BIT                  9

#define DMC_CAV_BLKMODE_MASK                3
#define DMC_CAV_BLKMODE_BIT                 24


#define DMC_CAV_LUT_ADDR_INDEX_MASK         0x7
#define DMC_CAV_LUT_ADDR_RD_EN              (1 << 8)
#define DMC_CAV_LUT_ADDR_WR_EN              (2 << 8)

#define DMC_CAV_YWRAP                       (1<<23)
#define DMC_CAV_XWRAP                       (1<<22)

typedef struct {
    zx_device_t*                            zxdev;
    platform_device_protocol_t              pdev;

    io_buffer_t                             dmc_regs;

    mtx_t                                   lock;

    canvas_protocol_t                       canvas;

    zx_handle_t                             bti;

    zx_handle_t                             pmt_handle[NUM_CANVAS_ENTRIES];
} aml_canvas_t;
