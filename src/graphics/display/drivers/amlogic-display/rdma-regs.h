// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_

#define VPU_RDMA_AHB_START_ADDR_MAN (0x1100 << 2)
#define VPU_RDMA_AHB_END_ADDR_MAN (0x1101 << 2)
#define VPU_RDMA_AHB_START_ADDR_1 (0x1102 << 2)
#define VPU_RDMA_AHB_END_ADDR_1 (0x1103 << 2)
#define VPU_RDMA_AHB_START_ADDR_2 (0x1104 << 2)
#define VPU_RDMA_AHB_END_ADDR_2 (0x1105 << 2)
#define VPU_RDMA_AHB_START_ADDR_3 (0x1106 << 2)
#define VPU_RDMA_AHB_END_ADDR_3 (0x1107 << 2)
#define VPU_RDMA_AHB_START_ADDR_4 (0x1108 << 2)
#define VPU_RDMA_AHB_END_ADDR_4 (0x1109 << 2)
#define VPU_RDMA_AHB_START_ADDR_5 (0x110a << 2)
#define VPU_RDMA_AHB_END_ADDR_5 (0x110b << 2)
#define VPU_RDMA_AHB_START_ADDR_6 (0x110c << 2)
#define VPU_RDMA_AHB_END_ADDR_6 (0x110d << 2)
#define VPU_RDMA_AHB_START_ADDR_7 (0x110e << 2)
#define VPU_RDMA_AHB_END_ADDR_7 (0x110f << 2)
#define VPU_RDMA_AHB_START_ADDR(x) (VPU_RDMA_AHB_START_ADDR_MAN + ((x + 1) << 3))
#define VPU_RDMA_AHB_END_ADDR(x) (VPU_RDMA_AHB_END_ADDR_MAN + ((x + 1) << 3))
#define VPU_RDMA_ACCESS_AUTO (0x1110 << 2)
#define VPU_RDMA_ACCESS_AUTO2 (0x1111 << 2)
#define VPU_RDMA_ACCESS_AUTO3 (0x1112 << 2)
#define VPU_RDMA_ACCESS_MAN (0x1113 << 2)
#define VPU_RDMA_CTRL (0x1114 << 2)
#define VPU_RDMA_STATUS (0x1115 << 2)
#define VPU_RDMA_STATUS2 (0x1116 << 2)
#define VPU_RDMA_STATUS3 (0x1117 << 2)

// VPU_RDMA_ACCESS_AUTO Bit Definition
#define RDMA_ACCESS_AUTO_INT_EN_ALL (0xFF << 8)
#define RDMA_ACCESS_AUTO_INT_EN(channel) (1 << ((channel + 1) << 3))
#define RDMA_ACCESS_AUTO_WRITE(channel) (1 << ((channel + 1) + 4))
#define RDMA_ACCESS_AUTO_INCREMENT(channel) (1 << (channel + 1))

// VPU_RDMA_CTRL Bit Definition
#define RMA_CTR_ALL_INT_DONE (0xFF << 24)
#define RDMA_CTRL_INT_DONE(channel) (1 << (24 + (channel + 1)))
#define RDMA_CTRL_WRITE_URGENT (1 << 7)
#define RDMA_CTRL_READ_URGENT (1 << 6)
#define RDMA_CTRL_WRITE_BURST_SIZE_4x16B (0 << 4)
#define RDMA_CTRL_WRITE_BURST_SIZE_8x16B (1 << 4)
#define RDMA_CTRL_WRITE_BURST_SIZE_12x16B (2 << 4)
#define RDMA_CTRL_WRITE_BURST_SIZE_16x16B (3 << 4)
#define RDMA_CTRL_READ_BURST_SIZE_4x16B (0 << 4)
#define RDMA_CTRL_READ_BURST_SIZE_8x16B (1 << 4)
#define RDMA_CTRL_READ_BURST_SIZE_12x16B (2 << 4)
#define RDMA_CTRL_READ_BURST_SIZE_16x16B (3 << 4)
#define RDMA_CTRL_RESET (1 << 1)
#define RDMA_CTRL_CLK_GATE_EN (1 << 0)

// VPU_RDMA_STATUS Bit Definition
#define RDMA_STATUS_BUSY (0x0003C0FF)
#define RDMA_STATUS_DONE(channel) (1 << (24 + (channel + 1)))

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_RDMA_REGS_H_
