// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_COMMON_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_COMMON_H_
#include <hwreg/mmio.h>

#define DISPLAY_MASK(start, count) (((1 << (count)) - 1) << (start))
#define DISPLAY_SET_MASK(mask, start, count, value) \
  ((mask & ~DISPLAY_MASK(start, count)) | (((value) << (start)) & DISPLAY_MASK(start, count)))

#define SET_BIT32(x, dest, value, start, count)                                    \
  WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~DISPLAY_MASK(start, count)) | \
                              (((value) << (start)) & DISPLAY_MASK(start, count)))

#define GET_BIT32(x, dest, start, count) \
  ((READ32_##x##_REG(dest) >> (start)) & ((1 << (count)) - 1))

#define SET_MASK32(x, dest, mask) WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) | mask))

#define CLEAR_MASK32(x, dest, mask) WRITE32_##x##_REG(dest, (READ32_##x##_REG(dest) & ~(mask)))

#define WRITE32_REG(x, a, v) WRITE32_##x##_REG(a, v)
#define READ32_REG(x, a) READ32_##x##_REG(a)

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...) zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

// Should match display_mmios table in board driver
enum {
  MMIO_VPU,
  MMIO_MPI_DSI,
  MMIO_DSI_PHY,
  MMIO_HHI,
  MMIO_AOBUS,
  MMIO_CBUS,
};

// Should match display_gpios table in board driver
enum {
  GPIO_BL,
  GPIO_LCD,
  GPIO_PANEL_DETECT,
  GPIO_HW_ID0,
  GPIO_HW_ID1,
  GPIO_HW_ID2,
  GPIO_COUNT,
};

// Should match display_irqs table in board driver
enum {
  IRQ_VSYNC,
  IRQ_RDMA,
  IRQ_VD1_WR,
};

enum CaptureState {
  CAPTURE_RESET = 0,
  CAPTURE_IDLE = 1,
  CAPTURE_ACTIVE = 2,
  CAPTURE_ERROR = 3,
};

constexpr uint8_t PANEL_DISPLAY_ID = 1;

// Astro Display dimension
constexpr uint32_t ASTRO_DISPLAY_WIDTH = 600;
constexpr uint32_t ASTRO_DISPLAY_HEIGHT = 1024;

// Sherlock Display dimension
constexpr uint32_t SHERLOCK_DISPLAY_WIDTH = 800;
constexpr uint32_t SHERLOCK_DISPLAY_HEIGHT = 1280;

constexpr bool kBootloaderDisplayEnabled = true;

// Supported panel types
constexpr uint8_t PANEL_TV070WSM_FT = 0x00;
constexpr uint8_t PANEL_P070ACB_FT = 0x01;
constexpr uint8_t PANEL_TV101WXM_FT = 0x02;
constexpr uint8_t PANEL_G101B158_FT = 0x03;
constexpr uint8_t PANEL_UNKNOWN = 0xff;

// This display driver supports EVT hardware and onwards. For pre-EVT boards,
// it will simply configure the framebuffer and canvas and assume U-Boot has
// already done all display initializations
constexpr uint8_t BOARD_REV_P1 = 0;
constexpr uint8_t BOARD_REV_P2 = 1;
constexpr uint8_t BOARD_REV_EVT_1 = 2;
constexpr uint8_t BOARD_REV_EVT_2 = 3;
constexpr uint8_t BOARD_REV_UNKNOWN = 0xff;

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_COMMON_H_
