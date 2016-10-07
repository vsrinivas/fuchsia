// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#define ASIX_VID 0x0B95
#define ASIX_PID 0x772B

// Vendor control requests
#define ASIX_REQ_SRAM_READ              0x02
#define ASIX_REQ_SRAM_WRITE             0x03
#define ASIX_REQ_SW_SERIAL_MGMT_CTRL    0x06
#define ASIX_REQ_PHY_READ               0x07
#define ASIX_REQ_PHY_WRITE              0x08
#define ASIX_REQ_SW_SERIAL_MGMT_STATUS  0x09
#define ASIX_REQ_HW_SERIAL_MGMT_CTRL    0x0A
#define ASIX_REQ_SROM_READ              0x0B
#define ASIX_REQ_SROM_WRITE             0x0C
#define ASIX_REQ_SROM_WRITE_ENABLE      0x0D
#define ASIX_REQ_SROM_WRITE_DISABLE     0x0E
#define ASIX_REQ_RX_CONTROL_READ        0x0F
#define ASIX_REQ_RX_CONTROL_WRITE       0x10
#define ASIX_REQ_IPG_READ               0x11
#define ASIX_REQ_IPG_WRITE              0x12
#define ASIX_REQ_NODE_ID_READ           0x13
#define ASIX_REQ_NODE_ID_WRITE          0x14
#define ASIX_REQ_MULTI_FIBER_READ       0x15
#define ASIX_REQ_MULTI_FIBER_WRITE      0x16
#define ASIX_REQ_TEST                   0x17
#define ASIX_REQ_PHY_ADDR               0x19
#define ASIX_REQ_MEDIUM_STATUS          0x1A
#define ASIX_REQ_MEDIUM_MODE            0x1B
#define ASIX_REQ_MONITOR_MODE_STATUS    0x1C
#define ASIX_REQ_MONITOR_MODE           0x1D
#define ASIX_REQ_GPIOS_STATUS           0x1E
#define ASIX_REQ_GPIOS                  0x1F
#define ASIX_REQ_SW_RESET               0x20
#define ASIX_REQ_SW_PHY_SELECT_STATUS   0x21
#define ASIX_REQ_SW_PHY_SELECT          0x22

// GPIOs
#define ASIX_GPIO_GPO0EN    0x01
#define ASIX_GPIO_GPO_0     0x02
#define ASIX_GPIO_GPO1EN    0x04
#define ASIX_GPIO_GPO_1     0x08
#define ASIX_GPIO_GPO2EN    0x10
#define ASIX_GPIO_GPO_2     0x20
#define ASIX_GPIO_RSE       0x80

// PHY registers
#define ASIX_PHY_BMCR       0
#define ASIX_PHY_BMSR       1
#define ASIX_PHY_PHYIDR1    2
#define ASIX_PHY_PHYIDR2    3
#define ASIX_PHY_ANAR       4
#define ASIX_PHY_ANLPAR     5
#define ASIX_PHY_ANER       6

// Reset register bits
#define ASIX_RESET_RR       0x01
#define ASIX_RESET_RT       0x02
#define ASIX_RESET_PRTE     0x04
#define ASIX_RESET_PRL      0x08
#define ASIX_RESET_BZ       0x10
#define ASIX_RESET_IPRL     0x20
#define ASIX_RESET_IPPD     0x40

// RX control bits
#define ASIX_RX_CTRL_PRO        0x01
#define ASIX_RX_CTRL_AMALL      0x02
#define ASIX_RX_CTRL_SEP        0x04
#define ASIX_RX_CTRL_AB         0x08
#define ASIX_RX_CTRL_AM         0x10
#define ASIX_RX_CTRL_AP         0x20
#define ASIX_RX_CTRL_S0         0x80

// IPG register defaults
#define ASIX_IPG_DEFAULT        0x15
#define ASIX_IPG1_DEFAULT       0x0C
#define ASIX_IPG2_DEFAULT       0x12

// Medium mode bits
#define ASIX_MEDIUM_MODE_GM     (1 << 0)
#define ASIX_MEDIUM_MODE_FD     (1 << 1)
#define ASIX_MEDIUM_MODE_AC     (1 << 2)
#define ASIX_MEDIUM_MODE_EN125  (1 << 3)
#define ASIX_MEDIUM_MODE_RFC    (1 << 4)
#define ASIX_MEDIUM_MODE_TFC    (1 << 5)
#define ASIX_MEDIUM_MODE_JFE    (1 << 6)
#define ASIX_MEDIUM_MODE_PF     (1 << 7)
#define ASIX_MEDIUM_MODE_RE     (1 << 8)
#define ASIX_MEDIUM_MODE_PS     (1 << 9)
#define ASIX_MEDIUM_MODE_JE     (1 << 10)
#define ASIX_MEDIUM_MODE_SBP    (1 << 11)
#define ASIX_MEDIUM_MODE_SM     (1 << 12)
