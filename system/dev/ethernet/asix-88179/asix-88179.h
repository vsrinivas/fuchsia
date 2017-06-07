// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#define ASIX_VID 0x0B95
#define AX88179_PID 0x1790

#define AX88179_PHY_ID   0x03

// Vendor commands
#define AX88179_REQ_MAC           0x01
#define AX88179_REQ_PHY           0x02
#define AX88179_REQ_WKUP          0x03
#define AX88179_REQ_NVS           0x04
#define AX88179_REQ_EFUSE         0x05
#define AX88179_REQ_EFUSE_RELOAD  0x06
#define AX88179_REQ_MFA           0x10
#define AX88179_REQ_USB           0x81
#define AX88179_REQ_WATCHDOG      0x91

// MAC registers
#define AX88179_MAC_
#define AX88179_MAC_PLSR    0x02
#define AX88179_MAC_GSR     0x03
#define AX88179_MAC_SMSR    0x04
#define AX88179_MAC_CSR     0x05
#define AX88179_MAC_EAR     0x07
#define AX88179_MAC_EDLR    0x08
#define AX88179_MAC_EDHR    0x09
#define AX88179_MAC_ECR     0x0a
#define AX88179_MAC_RCR     0x0b
#define AX88179_MAC_IPGCR   0x0d
#define AX88179_MAC_NIDR    0x10
#define AX88179_MAC_MFA     0x16
#define AX88179_MAC_TR      0x1e
#define AX88179_MAC_DSCR    0x20
#define AX88179_MAC_MSR     0x22
#define AX88179_MAC_MMSR    0x24
#define AX88179_MAC_GPIOCR  0x25
#define AX88179_MAC_EPPRCR  0x26
#define AX88179_MAC_JLCR    0x29
#define AX88179_MAC_VCR     0x2a
#define AX88179_MAC_RQCR    0x2e
#define AX88179_MAC_RQTLR   0x2f
#define AX88179_MAC_RQTHR   0x30
#define AX88179_MAC_RQSIZE  0x31
#define AX88179_MAC_RQIFGR  0x32
#define AX88179_MAC_CLKSR   0x33
#define AX88179_MAC_CRCR    0x34
#define AX88179_MAC_CTCR    0x35
#define AX88179_MAC_CPCR    0x36
#define AX88179_MAC_PWLHR   0x54
#define AX88179_MAC_PWLLR   0x55
#define AX88179_MAC_TXFBR   0x56
#define AX88179_MAC_RXFBLR  0x57
#define AX88179_MAC_RXFBHR  0x58
#define AX88179_MAC_PINCR   0x70
#define AX88179_MAC_LEDCR   0x73

// PHY registers
#define AX88179_PHY_BMCR    0x00
#define AX88179_PHY_BMSR    0x01
#define AX88179_PHY_PHYID1  0x02
#define AX88179_PHY_PHYID2  0x03
#define AX88179_PHY_ANAR    0x04
#define AX88179_PHY_ANLPAR  0x05
#define AX88179_PHY_ANER    0x06
#define AX88179_PHY_ANNPTR  0x07
#define AX88179_PHY_ANNPRR  0x08
#define AX88179_PHY_GBCR    0x09
#define AX88179_PHY_GBSR    0x0a
#define AX88179_PHY_MACR    0x0d
#define AX88179_PHY_MAADR   0x0e
#define AX88179_PHY_GBESR   0x0f
#define AX88179_PHY_PHYCR   0x10
#define AX88179_PHY_PHYSR   0x11
#define AX88179_PHY_INER    0x12
#define AX88179_PHY_INSR    0x13
#define AX88179_PHY_RXERC   0x18
#define AX88179_PHY_PAGSEL  0x1f
#define AX88179_PHY_ELEDIR1 0x05
#define AX88179_PHY_ELEDIR2 0x06
#define AX88179_PHY_EPAGSR  0x1e
#define AX88179_PHY_LEDACR  0x1a
#define AX88179_PHY_LEDCR   0x1c

#define AX88179_PHY_MMD_PC1R    0x00
#define AX88179_PHY_MMD_PS1R    0x01
#define AX88179_PHY_MMD_EEECR   0x14
#define AX88179_PHY_MMD_EEEWER  0x16
#define AX88179_PHY_MMD_EEEAR   0x3c
#define AX88179_PHY_MMD_EEELPAR 0x3d

// USB registers
#define AX88179_USB_EP2FIFO  0x5c
#define AX88179_USB_EP3FIFO  0x8c
#define AX88179_USB_U1U2CTL  0x310

// Register bits  -- define as needed
#define AX88179_PLSR_USB_FS  (1 << 0)
#define AX88179_PLSR_USB_HS  (1 << 1)
#define AX88179_PLSR_USB_SS  (1 << 2)
#define AX88179_PLSR_USB_MASK (AX88179_PLSR_USB_FS|AX88179_PLSR_USB_HS|AX88179_PLSR_USB_SS)

#define AX88179_PLSR_EPHY_10   (1 << 4)
#define AX88179_PLSR_EPHY_100  (1 << 5)
#define AX88179_PLSR_EPHY_1000 (1 << 6)
#define AX88179_PLSR_EPHY_MASK (AX88179_PLSR_EPHY_10|AX88179_PLSR_EPHY_100|AX88179_PLSR_EPHY_1000)

#define AX88179_PHYSR_SPEED  (3 << 14)
#define AX88179_PHYSR_DUPLEX (1 << 13)

// Headers
#define AX88179_RX_DROPPKT   0x80000000
#define AX88179_RX_MIIER     0x40000000
#define AX88179_RX_CRCER     0x20000000
#define AX88179_RX_PKTLEN    0x1fff0000
#define AX88179_RX_BMC       0x00008000
#define AX88179_RX_VLANPRI   0x00007000
#define AX88179_RX_OK        0x00000800
#define AX88179_RX_VLANIND   0x00000700
#define AX88179_RX_COE       0x000000ff

//#define AX88179_RX_COE_???        0x80
#define AX88179_RX_COE_L3_MASK      0x60
#define AX88179_RX_COE_L3_IPV6      0x40
#define AX88179_RX_COE_L3_IPV4      0x20

#define AX88179_RX_COE_L4_MASK      0x1c
#define AX88179_RX_COE_L4_ICMPv6    0x11
#define AX88179_RX_COE_L4_TCP       0x10
#define AX88179_RX_COE_L4_IGMP      0x0c
#define AX88179_RX_COE_L4_ICMP      0x08
#define AX88179_RX_COE_L4_UDP       0x04

#define AX88179_RX_COE_L3_CKSUM_ERR 0x02
#define AX88179_RX_COE_L4_CKSUM_ERR 0x01
