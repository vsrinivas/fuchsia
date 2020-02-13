// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_COMMON_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_COMMON_H_
#include <zircon/pixelformat.h>

#include <ddk/protocol/display/controller.h>
#include <hwreg/mmio.h>

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...) zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

// Should match display_mmios table in board driver
enum {
  MMIO_DISP_OVL,
  MMIO_DISP_RDMA,
  MMIO_DISP_MIPITX,
  MMIO_DISP_MUTEX,
  MMIO_DISP_SYSCFG,
  MMIO_DISP_COLOR,
  MMIO_DISP_AAL,
  MMIO_DISP_DITHER,
  MMIO_DISP_GAMMA,
  MMIO_DISP_CCORR,
  MMIO_DISP_SMI_LARB0,
};

constexpr uint8_t PANEL_DISPLAY_ID = 1;

// mt8167s_ref Display dimension
constexpr uint32_t MTKREF_DISPLAY_WIDTH = 720;
constexpr uint32_t MTKREF_DISPLAY_HEIGHT = 1280;

// cleo display dimension
constexpr uint32_t CLEO_DISPLAY_WIDTH = 480;
constexpr uint32_t CLEO_DISPLAY_HEIGHT = 800;

// This is the absolute maximum height and width supported by the Display Subsystem
constexpr uint16_t kMaxWidth = 4095;
constexpr uint16_t kMaxHeight = 4095;

// Supported panel types
constexpr uint8_t PANEL_ILI9881C = 0x00;
constexpr uint8_t PANEL_ST7701S = 0x01;

struct OvlConfig {
  zx_pixel_format_t format;
  zx_paddr_t paddr;
  uint64_t handle;
  alpha_t alpha_mode;
  float alpha_val;
  frame_t src_frame;
  frame_t dest_frame;
  uint32_t pitch;
  frame_transform_t transform;
};

enum SysConfigModule {
  MODULE_OVL0,
  MODULE_RDMA0,
  MODULE_COLOR0,
  MODULE_CCORR,
  MODULE_AAL,
  MODULE_GAMMA,
  MODULE_DITHER,
  MODULE_PWM0,
  MODULE_DSI0,
  MODULE_SMI,
  MODULE_CONFIG,
  MODULE_CMDQ,
  MODULE_MUTEX,
  MODULE_SMI_COMMON,
  MODULE_NUM,
};

enum MutexMode {
  MUTEX_SINGLE,
  MUTEX_DSI0,
  MUTEX_DPI0,
  MUTEX_DPI1,
};

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_COMMON_H_
