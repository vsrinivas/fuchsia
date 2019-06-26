// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_OVL_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_OVL_H_

// clang-format off
#define OVL_STA                         (0x0000)
#define OVL_INTEN                       (0x0004)
#define OVL_INTSTA                      (0x0008)
#define OVL_EN                          (0x000C)
#define OVL_TRIG                        (0x0010)
#define OVL_RST                         (0x0014)
#define OVL_ROI_SIZE                    (0x0020)
#define OVL_DATAPATH_CON                (0x0024)
#define OVL_ROI_BGCLR                   (0x0028)
#define OVL_SRC_CON                     (0x002C)
#define OVL_Lx_CON(x)                   (0x0030 + (0x20 * x))
#define OVL_Lx_SRCKEY(x)                (0x0034 + (0x20 * x))
#define OVL_Lx_SRC_SIZE(x)              (0x0038 + (0x20 * x))
#define OVL_Lx_OFFSET(x)                (0x003C + (0x20 * x))
#define OVL_Lx_ADDR(x)                  (0x0F40 + (0x20 * x))
#define OVL_Lx_PITCH(x)                 (0x0044 + (0x20 * x))
#define OVL_Lx_TILE(x)                  (0x0048 + (0x20 * x))
#define OVL_RDMAx_CTRL(x)               (0x00C0 + (0x20 * x))
#define OVL_RDMAx_MEM_GMC_SETTING(x)    (0x00C8 + (0x20 * x))
#define OVL_RDMAx_MEM_SLOW_CON(x)       (0x00CC + (0x20 * x))
#define OVL_RDMAx_FIFO_CTRL(x)          (0x00D0 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_R0(x)           (0x0134 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_R1(x)           (0x0138 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_G0(x)           (0x013C + (0x20 * x))
#define OVL_Lx_Y2R_PARA_G1(x)           (0x0140 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_B0(x)           (0x0144 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_B1(x)           (0x0148 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_YUV_A_0(x)      (0x014C + (0x20 * x))
#define OVL_Lx_Y2R_PARA_YUV_A_1(x)      (0x0150 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_RGB_A_0(x)      (0x0154 + (0x20 * x))
#define OVL_Lx_Y2R_PARA_RGB_A_1(x)      (0x0158 + (0x20 * x))
#define OVL_DEBUG_MON_SEL               (0x01D4)
#define OVL_RDMAx_MEM_GMC_S2(x)         (0x01E0 + (0x04 * x))
#define OVL_DUMMY_REG                   (0x0200)
#define OVL_SMI_DBG                     (0x0230)
#define OVL_GREQ_LAYER_CNT              (0x0234)
#define OVL_FLOW_CTRL_DBG               (0x0240)
#define OVL_ADDCON_DBG                  (0x0244)
#define OVL_RDMAx_DBG(x)                (0x024C + (0x04 * x))
#define OVL_Lx_CLR(x)                   (0x025C + (0x04 * x))

// OVL_INTEN Bit Definitions
#define INT_FRAME_COMPLETE              (0xe)

// OVL_FLOW_CTRL_DBG Bit Definitions
#define OVL_IDLE                        (0x1)
#define OVL_IDLE2                       (0x2)

// OVL_SRC_CON Bit Definitions
#define SRC_CON_ENABLE_LAYER(x)         (1 << x)

// OVL_Lx_CON Bit Definitions
#define Lx_CON_BYTE_SWAP                (1 << 24)
#define Lx_CON_CLRFMT(x)                (x << 12)
#define Lx_CON_HFE                      (1 << 10)
#define Lx_CON_VFE                      (1 << 9)
#define Lx_CON_AEN                      (1 << 8)
#define Lx_CON_ALPHA(x)                 (x << 0)

// OVL_Lx_PITCH Bit Definitions
#define Lx_PITCH_SRFL_EN                (1 << 31)
#define Lx_PITCH_DST_ALPHA(x)           (x << 20)
#define Lx_PITCH_SRC_ALPHA(x)           (x << 16)
#define Lx_PITCH_PITCH(x)               (x << 0)

#define NO_SRC_ALPHA                    (0x0)
#define SRC_ALPHA                       (0x5)
#define INV_SRC_ALPHA                   (0xA)


// Color Format based on OVL_Lx_CON Register definition
enum {
    RGB565 = 0,
    RGB888 = 1,
    RGBA8888 = 2,
    BGRA8888 = 2, // same as RGBA8888
    ARGB8888 = 3,
    ABGR8888 = 3, // same as ABGR8888
    UVVY = 4,
    VYUY = 4, // same as UVVY
    YUYV = 5,
    YVYU = 5, // same as YUYV
    UNKNOWN_FORMAT = 0xFFFFFFFF,
};

// Color Matrix Table based on OVL_Lx_CON Register definition
// TODO(payamm): This is only really needed if we ever support YUV
enum {
    JPEG_TO_RGB = 4, // for YUV space
    BT601_TO_RGB = 6,
    BT709_TO_RGB = 7,
};

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_OVL_H_
