// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#pragma once

// clang-format off
// Please don't touch my columns.

#define APP_LTSSM_ENABLE                      (1 << 7)

#define PORT_LINK_CTRL_OFF                    (0x710)
#define PLC_VENDOR_SPECIFIC_DLLP_REQ          (1 << 0)
#define PLC_SCRAMBLE_DISABLE                  (1 << 1)
#define PLC_LOOPBACK_ENABLE                   (1 << 2)
#define PLC_RESET_ASSERT                      (1 << 3)
#define PLC_DLL_LINK_EN                       (1 << 5)
#define PLC_LINK_DISABLE                      (1 << 6)
#define PLC_FAST_LINK_MODE                    (1 << 7)
#define PLC_LINK_RATE_MASK                    (0xF << 8)
#define PLC_LINK_CAPABLE_MASK                 (0x3F << 16)
    #define PLC_LINK_CAPABLE_X1               (0x01 << 16)
    #define PLC_LINK_CAPABLE_X2               (0x03 << 16)
    #define PLC_LINK_CAPABLE_X4               (0x07 << 16)
    #define PLC_LINK_CAPABLE_X8               (0x0f << 16)
    #define PLC_LINK_CAPABLE_X16              (0x1f << 16)
#define PLC_BEACON_ENABLE                     (1 << 24)
#define PLC_CORRUPT_LCRC_ENABLE               (1 << 25)
#define PLC_EXTENDED_SYNC_H                   (1 << 26)
#define PLC_TRANSMIT_LANE_REVERSAL_ENABLE     (1 << 27)

#define GEN2_CTRL_OFF                         (0x80C)
#define G2_CTRL_FAST_TRAINING_SEQ_MASK        (0xFF << 0)
#define G2_CTRL_NUM_OF_LANES_MASK             (0x1F << 8)
    #define G2_CTRL_NO_OF_LANES(x)            ((x) << 8)
#define G2_CTRL_PRE_DET_LANE_MASK             (0x07 << 13)
#define G2_CTRL_AUTO_LANE_FLIP_CTRL_EN        (1 << 16)
#define G2_CTRL_DIRECT_SPEED_CHANGE           (1 << 17)
#define G2_CTRL_CONFIG_PHY_TX_CHANGE          (1 << 18)
#define G2_CTRL_CONFIG_TX_COMP_RX             (1 << 19)
#define G2_CTRL_SEL_DEEMPHASIS                (1 << 20)
#define G2_CTRL_GEN1_EI_INFERENCE             (1 << 21)

#define PCIE_CTRL_STS_OFF                     (0x78)
#define PCIE_CAP_MAX_PAYLOAD_SIZE_CS_MASK     (0x7 << 5)

#define PCIE_TYPE1_STS_CMD_OFF                (0x04)
#define PCIE_TYPE1_STS_CMD_IO_ENABLE          (1 << 0)
#define PCIE_TYPE1_STS_CMD_MEM_SPACE_ENABLE   (1 << 1)
#define PCIE_TYPE1_STS_CMD_BUS_MASTER_ENABLE  (1 << 2)

#define PCIE_CFG_STATUS12                     (0x30)
    #define PCIE_CFG12_SMLH_UP                (0x01 << 6)
    #define PCIE_CFG12_RDLH_UP                (0x01 << 16)
    #define PCIE_CFG12_LTSSM_MASK             (0x1f << 10)
    #define PCIE_CFG12_LTSSM_UP               (0x11 << 10)

#define ATU_REGION_COUNT                      (16)
#define ATU_REGION_CTRL2_ENABLE               (1 << 31)
#define ATU_CFG_SHIFT_MODE                    (1 << 28)
#define ATU_PROGRAM_RETRIES                   (5)
#define ATU_WAIT_ENABLE_TIMEOUT_US            (10000)
#define ATU_MIN_REGION_SIZE                   (1024 * 64)       // 64 KiB

#define PCIE_TLP_TYPE_MEM_RW                  (0x00)
#define PCIE_TLP_TYPE_MEM_RD_LOCKED           (0x01)
#define PCIE_TLP_TYPE_IO_RW                   (0x02)
#define PCIE_TLP_TYPE_CFG0                    (0x04)
#define PCIE_TLP_TYPE_CFG1                    (0x05)
#define PCIE_ECAM_SIZE                        (0x1000)

#define PCIE_HEADER_TYPE_MASK                 (0x7f)
#define PCIE_HEADER_TYPE0                     (0x0)
#define PCIE_HEADER_TYPE1                     (0x1)
#define PCIE_HEADER_TYPE1                     (0x1)
#define PCIE_HEADER_BUS_REG_OFF               (0x18)

#define RST_PCIE_A       (0x1 << 1)
#define RST_PCIE_B       (0x1 << 2)
#define RST_PCIE_APB     (0x1 << 6)
#define RST_PCIE_PHY     (0x1 << 7)

#define PCI_TYPE1_BAR0  (0x10)
#define PCI_TYPE1_BAR1  (0x14)

typedef struct atu_ctrl_regs {
    uint32_t region_ctrl1;
    uint32_t region_ctrl2;
    uint32_t unroll_lower_base;
    uint32_t unroll_upper_base;
    uint32_t unroll_limit;
    uint32_t unroll_lower_target;
    uint32_t unroll_upper_target;
} __PACKED atu_ctrl_regs_t;

typedef struct pci_bus_reg {
    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t secondary_lat_timer;
}  __PACKED pci_bus_reg_t;
