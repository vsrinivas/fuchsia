// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#define SMSC_VID                        (0x0424)
#define SMSC_9514_LAN_PID               (0xec00)

#define LAN9514_PHY_ID                  (0x0001)

#define LAN9514_REQ_REG_WRITE           (0xa0)
#define LAN9514_REQ_REG_READ            (0xa1)


#define LAN9514_RXSTATUS_FILT_FAIL      (0x40000000)
#define LAN9514_RXSTATUS_FRAME_LEN      (0x3fff0000)
#define LAN9514_RXSTATUS_ERROR_STAT     (0x00008000)
#define LAN9514_RXSTATUS_BCAST_FRAME    (0x00002000)
#define LAN9514_RXSTATUS_LEN_ERR        (0x00001000)
#define LAN9514_RXSTATUS_RUNT_FRAME     (0x00000800)
#define LAN9514_RXSTATUS_MCAST_FRAME    (0x00000400)
#define LAN9514_RXSTATUS_FRAME_LONG     (0x00000080)
#define LAN9514_RXSTATUS_COLLISION      (0x00000040)
#define LAN9514_RXSTATUS_FRAME_TYPE     (0x00000020)
#define LAN9514_RXSTATUS_RX_WDOG        (0x00000010)
#define LAN9514_RXSTATUS_MII_ERR        (0x00000008)
#define LAN9514_RXSTATUS_DRIBBLE        (0x00000004)
#define LAN9514_RXSTATUS_CRC_ERR        (0x00000002)
#define LAN9514_RXSTATUS_ERROR_MASK     ( LAN9514_RXSTATUS_FILT_FAIL  | LAN9514_RXSTATUS_ERROR_STAT | \
                                          LAN9514_RXSTATUS_LEN_ERR    | LAN9514_RXSTATUS_RUNT_FRAME | \
                                          LAN9514_RXSTATUS_FRAME_LONG | LAN9514_RXSTATUS_COLLISION  | \
                                          LAN9514_RXSTATUS_RX_WDOG    | LAN9514_RXSTATUS_MII_ERR     | \
                                          LAN9514_RXSTATUS_CRC_ERR )

/* LAN9514 control registers */

#define LAN9514_ID_REV_REG              (0x00)

#define LAN9514_INT_STS_REG             (0x08)
#define LAN9514_INT_STS_REG_CLEAR_ALL   (0xffffffff)

#define LAN9514_TX_CFG_REG              (0x10)
#define LAN9514_TX_CFG_ON               (0x00000004)
#define LAN9514_TX_CFG_STOP             (0x00000002)
#define LAN9514_TX_CFG_FIFO_FLUSH       (0x00000001)

#define LAN9514_HW_CFG_REG              (0x14)
#define LAN9514_HW_CFG_LRST             (0x00000008)
#define LAN9514_HW_CFG_BIR              (0x00001000)
#define LAN9514_HW_CFG_RXDOFF           (0x00000600)

#define LAN9514_PM_CTRL_REG             (0x20)
#define LAN9514_PM_CTRL_PHY_RST         (0x00000010)

#define LAN9514_LED_GPIO_CFG_REG        (0x24)
#define LAN9514_LED_GPIO_CFG_SPD_LED    (0x01000000)
#define LAN9514_LED_GPIO_CFG_LNK_LED    (0x00100000)
#define LAN9514_LED_GPIO_CFG_FDX_LED    (0x00010000)

#define LAN9514_AFC_CFG_REG             (0x2C)
/* Hi watermark = 15.5Kb (~10 mtu pkts) */
/* low watermark = 3k (~2 mtu pkts) */
/* backpressure duration = ~ 350us */
/* Apply FC on any frame. */
#define LAN9514_AFC_CFG_DEFAULT         (0x00F830A1)


#define LAN9514_INT_EP_CTL_REG          (0x68)
#define LAN9514_INT_EP_CTL_INTEP        (0x80000000)
#define LAN9514_INT_EP_CTL_MACRTO       (0x00080000)
#define LAN9514_INT_EP_CTL_TX_STOP      (0x00020000)
#define LAN9514_INT_EP_CTL_RX_STOP      (0x00010000)
#define LAN9514_INT_EP_CTL_PHY_INT      (0x00008000)
#define LAN9514_INT_EP_CTL_TXE          (0x00004000)
#define LAN9514_INT_EP_CTL_TDFU         (0x00002000)
#define LAN9514_INT_EP_CTL_TDFO         (0x00001000)
#define LAN9514_INT_EP_CTL_RXDF         (0x00000800)
#define LAN9514_INT_EP_CTL_GPIOS        (0x000007FF)




#define LAN9514_BULK_IN_DLY_REG         (0x6c)
#define LAN9514_BULK_IN_DLY_DEFAULT     (0x00002000)

#define LAN9514_MAC_CR_REG              (0x100)
#define LAN9514_MAC_CR_RXALL            (0x80000000)
#define LAN9514_MAC_CR_RCVOWN           (0x00800000)
#define LAN9514_MAC_CR_LOOPBK           (0x00200000)
#define LAN9514_MAC_CR_FDPX             (0x00100000)
#define LAN9514_MAC_CR_MCPAS            (0x00080000)
#define LAN9514_MAC_CR_PRMS             (0x00040000)
#define LAN9514_MAC_CR_INVFILT          (0x00020000)
#define LAN9514_MAC_CR_PASSBAD          (0x00010000)
#define LAN9514_MAC_CR_HFILT            (0x00008000)
#define LAN9514_MAC_CR_HPFILT           (0x00002000)
#define LAN9514_MAC_CR_LCOLL            (0x00001000)
#define LAN9514_MAC_CR_BCAST            (0x00000800)
#define LAN9514_MAC_CR_DISRTY           (0x00000400)
#define LAN9514_MAC_CR_PADSTR           (0x00000100)
#define LAN9514_MAC_CR_BOLMT_MASK       (0x000000C0)
#define LAN9514_MAC_CR_DFCHK            (0x00000020)
#define LAN9514_MAC_CR_TXEN             (0x00000008)
#define LAN9514_MAC_CR_RXEN             (0x00000004)


#define LAN9514_ADDR_HI_REG             (0x104)
#define LAN9514_ADDR_LO_REG             (0x108)

#define LAN9514_MII_ACCESS_REG          (0x114)
#define LAN9514_MII_ACCESS_MIIBZY       (0x00000001)
#define LAN9514_MII_ACCESS_MIIWnR       (0x00000002)

#define LAN9514_MII_DATA_REG            (0x118)

#define LAN9514_COE_CR_REG              (0x130)
#define LAN9514_COE_CR_TX_COE_EN        (0x00010000)
#define LAN9514_COE_CR_RX_COE_EN        (0x00000001)



/* MII - Basic mode control register and bit fields */
#define MII_PHY_BMCR_REG                    (0x00)
#define MII_PHY_BMCR_RESV                   (0x003f)
#define MII_PHY_BMCR_SPEED1000              0x0040  /* MSB of Speed (1000)         */
#define MII_PHY_BMCR_CTST                   0x0080  /* Collision test              */
#define MII_PHY_BMCR_FULLDPLX               0x0100  /* Full duplex                 */
#define MII_PHY_BMCR_ANRESTART              0x0200  /* Auto negotiation restart    */
#define MII_PHY_BMCR_ISOLATE                0x0400  /* Isolate data paths from MII */
#define MII_PHY_BMCR_PDOWN                  0x0800  /* Enable low power state      */
#define MII_PHY_BMCR_ANENABLE               0x1000  /* Enable auto negotiation     */
#define MII_PHY_BMCR_SPEED100               0x2000  /* Select 100Mbps              */
#define MII_PHY_BMCR_LOOPBACK               0x4000  /* TXD loopback bits           */
#define MII_PHY_BMCR_RESET                  (0x8000)


#define MII_PHY_BSR_REG                 (0x01)
#define MII_PHY_BSR_LINK_UP             (0x0004)


#define MII_PHY_ADVERTISE_REG               (0x04)  /* Advertisement control reg   */
#define MII_PHY_ADVERTISE_SLCT              0x001f  /* Selector bits               */
#define MII_PHY_ADVERTISE_CSMA              0x0001  /* Only selector supported     */
#define MII_PHY_ADVERTISE_10HALF            0x0020  /* Try for 10mbps half-duplex  */
#define MII_PHY_ADVERTISE_1000XFULL         0x0020  /* Try for 1000BASE-X full-duplex */
#define MII_PHY_ADVERTISE_10FULL            0x0040  /* Try for 10mbps full-duplex  */
#define MII_PHY_ADVERTISE_1000XHALF         0x0040  /* Try for 1000BASE-X half-duplex */
#define MII_PHY_ADVERTISE_100HALF           0x0080  /* Try for 100mbps half-duplex */
#define MII_PHY_ADVERTISE_1000XPAUSE        0x0080  /* Try for 1000BASE-X pause    */
#define MII_PHY_ADVERTISE_100FULL           0x0100  /* Try for 100mbps full-duplex */
#define MII_PHY_ADVERTISE_1000XPSE_ASYM     0x0100  /* Try for 1000BASE-X asym pause */
#define MII_PHY_ADVERTISE_100BASE4          0x0200  /* Try for 100mbps 4k packets  */
#define MII_PHY_ADVERTISE_PAUSE_CAP         0x0400  /* Try for pause               */
#define MII_PHY_ADVERTISE_PAUSE_ASYM        0x0800  /* Try for asymetric pause     */
#define MII_PHY_ADVERTISE_RESV              0x1000  /* Unused...                   */
#define MII_PHY_ADVERTISE_RFAULT            0x2000  /* Say we can detect faults    */
#define MII_PHY_ADVERTISE_LPACK             0x4000  /* Ack link partners response  */
#define MII_PHY_ADVERTISE_NPAGE             0x8000  /* Next page bit               */

#define MII_PHY_ADVERTISE_FULL              ( MII_PHY_ADVERTISE_100FULL | MII_PHY_ADVERTISE_10FULL | \
                                              MII_PHY_ADVERTISE_CSMA)
#define MII_PHY_ADVERTISE_ALL               ( MII_PHY_ADVERTISE_10HALF  | MII_PHY_ADVERTISE_10FULL | \
                                              MII_PHY_ADVERTISE_100HALF | MII_PHY_ADVERTISE_100FULL)

#define MII_PHY_LAN9514_ANEG_EXP_REG            (0x06)

// Chip specific (proprietary) MII registers

// Interrupt source
#define MII_PHY_LAN9514_INT_SRC_REG             (29)
#define MII_PHY_LAN9514_INT_SRC_ENERGY_ON       (0x0080)
#define MII_PHY_LAN9514_INT_SRC_ANEG_COMP       (0x0040)
#define MII_PHY_LAN9514_INT_SRC_REMOTE_FAULT    (0x0020)
#define MII_PHY_LAN9514_INT_SRC_LINK_DOWN       (0x0010)


// Interrupt mask
#define MII_PHY_LAN9514_INT_MASK_REG            (30)
#define MII_PHY_LAN9514_INT_MASK_ENERGY_ON      (0x0080)
#define MII_PHY_LAN9514_INT_MASK_ANEG_COMP      (0x0040)
#define MII_PHY_LAN9514_INT_MASK_REMOTE_FAULT   (0x0020)
#define MII_PHY_LAN9514_INT_MASK_LINK_DOWN      (0x0010)
#define MII_PHY_LAN9514_INT_MASK_DEFAULT        (MII_PHY_LAN9514_INT_MASK_ANEG_COMP | \
                                                 MII_PHY_LAN9514_INT_MASK_LINK_DOWN )

