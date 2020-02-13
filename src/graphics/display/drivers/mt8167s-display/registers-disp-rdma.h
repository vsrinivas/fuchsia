// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DISP_RDMA_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DISP_RDMA_H_

// clang-format off
#define DISP_RDMA_INT_ENABLE                    (0x0000)
#define DISP_RDMA_INT_STATUS                    (0x0004)
#define DISP_RDMA_GLOBAL_CON                    (0x0010)
#define DISP_RDMA_SIZE_CON0                     (0x0014)
#define DISP_RDMA_SIZE_CON1                     (0x0018)
#define DISP_RDMA_TARGET_LINE                   (0x001C)
#define DISP_RDMA_MEM_CON                       (0x0024)
#define DISP_RDMA_MEM_SRC_PITCH                 (0x002C)
#define DISP_RDMA_MEM_GMC_SETTING_0             (0x0030)
#define DISP_RDMA_MEM_SLOW_CON                  (0x0034)
#define DISP_RDMA_MEM_GMC_SETTING_1             (0x0038)
#define DISP_RDMA_FIFO_CON                      (0x0040)
#define DISP_RDMA_FIFO_LOG                      (0x0044)
#define DISP_RDMA_C00                           (0x0054)
#define DISP_RDMA_C01                           (0x0058)
#define DISP_RDMA_C02                           (0x005C)
#define DISP_RDMA_C10                           (0x0060)
#define DISP_RDMA_C11                           (0x0064)
#define DISP_RDMA_C12                           (0x0068)
#define DISP_RDMA_C20                           (0x006C)
#define DISP_RDMA_C21                           (0x0070)
#define DISP_RDMA_C22                           (0x0074)
#define DISP_RDMA_PRE_ADD_0                     (0x0078)
#define DISP_RDMA_PRE_ADD_1                     (0x007C)
#define DISP_RDMA_PRE_ADD_2                     (0x0080)
#define DISP_RDMA_POST_ADD_0                    (0x0084)
#define DISP_RDMA_POST_ADD_1                    (0x0088)
#define DISP_RDMA_POST_ADD_2                    (0x008C)
#define DISP_RDMA_DUMMY                         (0x0090)
#define DISP_RDMA_DEBUG_OUT_SEL                 (0x0094)
#define DISP_RDMA_BG_CON_0                      (0x00a0)
#define DISP_RDMA_BG_CON_1                      (0x00a4)
#define DISP_RDMA_THRESHOLD_FOR_SODI            (0x00a8)
#define DISP_RDMA_IN_P_CNT                      (0x00f0)
#define DISP_RDMA_IN_LINE_CNT                   (0x00f4)
#define DISP_RDMA_OUT_P_CNT                     (0x00f8)
#define DISP_RDMA_OUT_LINE_CNT                  (0x00fc)
#define DISP_RDMA_MEM_START_ADDR                (0x0f00)

// DISP_RDMA_GLOBAL_CON Bit Definitions
#define GLOBAL_CON_RESET_STATE_SHIFT            (8)
#define GLOBAL_CON_RESET_STATE_MASK             (0x7 << GLOBAL_CON_RESET_STATE_SHIFT)
#define GLOBAL_CON_RESTE_STATE_IDLE             (0x100)
#define GLOBAL_CON_SOFT_RESET                   (1 << 4)
#define GLOBAL_CON_MODE_SEL_DIRECT_LINK         (0 << 1)
#define GLOBAL_CON_ENGINE_EN                    (1 << 0)

// DISP_RDMA_SIZE_CON0 Bit Definitions
#define SIZE_CON0_WIDTH(x)                      ((x & 0x1FFF) << 0)

// DISP_RDMA_SIZE_CON1 Bit Definitions
#define SIZE_CON1_HEIGHT(x)                     ((x & 0xFFFFF) << 0)

// DISP_RDMA_FIFO_CON Bit Definitions
#define FIFO_CON_CLEAR_MASK                     (0x800003FF)
#define FIFO_CON_UNDERFLOW_EN                   (1 << 31)
#define FIFO_CON_FIFO_THRESHOLD_DEFAULT         (16 << 0)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_MT8167S_DISPLAY_REGISTERS_DISP_RDMA_H_
