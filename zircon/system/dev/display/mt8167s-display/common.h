// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <hwreg/mmio.h>
#include <zircon/pixelformat.h>
#include <ddk/protocol/display/controller.h>

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...) zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

// Should match display_mmios table in board driver
enum {
    MMIO_DISP_OVL,
    MMIO_DISP_RDMA,
    MMIO_DISP_MIPITX,
};

constexpr uint8_t PANEL_DISPLAY_ID = 1;

// mt8167s_ref Display dimension
constexpr uint32_t DISPLAY_WIDTH = 720;
constexpr uint32_t DISPLAY_HEIGHT = 1280;

// This is the absolute maximum height and width supported by the Display Subsystem
constexpr uint16_t kMaxWidth = 4095;
constexpr uint16_t kMaxHeight = 4095;

struct OvlConfig {
    zx_pixel_format_t   format;
    zx_paddr_t          paddr;
    alpha_t             alpha_mode;
    float               alpha_val;
    frame_t             src_frame;
    frame_t             dest_frame;
    uint32_t            pitch;
    frame_transform_t   transform;
};
