// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* AmLogic proprietary registers */
#define REG2_ETH_REG2_REVERSED (1 << 28)
#define REG2_INTERNAL_PHY_ID (0x110181)

#define REG3_PHY_ENABLE (1 << 31)
#define REG3_USE_PHY_IP (1 << 30)
#define REG3_CLK_IN_EN (1 << 29)
#define REG3_USE_PHY_MDI (1 << 26)
#define REG3_LED_POLARITY (1 << 23)
#define REG3_ETH_REG3_19_RESVERD (0x9 << 16)
#define REG3_CFG_PHY_ADDR (0x8 << 8)
#define REG3_CFG_MODE (0x7 << 4)
#define REG3_CFG_EN_HIGH (1 << 3)
#define REG3_ETH_REG3_2_RESERVED (0x7)
