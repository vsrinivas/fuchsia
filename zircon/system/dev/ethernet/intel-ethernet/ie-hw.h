// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// See: PCI/PCI-X Family of Gigabit Ethernet Controllers
//      Software Developer's Manual
//      317453006EN.PDF
//      Revision 4.0

#define IE_CTRL      0x0000 // Device Control
#define IE_STATUS    0x0008 // Device Status
#define IE_CTRL_EXT  0x0018 // Extended Device Control
#define IE_MDIC      0x0020 // MDI control (PHY access)
#define IE_TXCW      0x0178 // TX Config Word
#define IE_RXCW      0x0180 // RX Config Word
#define IE_ICR       0x00c0 // Interrupt Cause Read
#define IE_ICS       0x00c8 // Interrupt Cause Set
#define IE_IMS       0x00d0 // Interrupt Mask Set / Read
#define IE_IMC       0x00d8 // Interrupt Mask Clear

#define IE_RCTL      0x0100 // Receive Control
#define IE_RDBAL     0x2800 // RX Descriptor Base Low
#define IE_RDBAH     0x2804 // RX Descriptor Base High
#define IE_RDLEN     0x2808 // RX Descriptor Length
#define IE_RDH       0x2810 // RX Descriptor Head
#define IE_RDT       0x2818 // RX Descriptor Tail
#define IE_RDTR      0x3820 // RX Delay Timer

#define IE_TCTL      0x0400 // Transmit Control
#define IE_TIPG      0x0410 // TX IPG
#define IE_TDBAL     0x3800 // TX Descriptor Base Low
#define IE_TDBAH     0x3804 // TX Descriptor Base High
#define IE_TDLEN     0x3808 // TX Descriptor Length
#define IE_TDH       0x3810 // TX Descriptor Head
#define IE_TDT       0x3818 // TX Descriptor Tail
#define IE_TIDV      0x3820 // TX Interrupt Delay Value

#define IE_TXDMAC    0x3000 // TX DMA Control
#define IE_TXDCTL    0x3828 // TX Descriptor Control
#define IE_RXDCTL    0x2828 // RX Descriptor Control

#define IE_RXCSUM    0x5000 // RX Checksum Control
#define IE_MTA(n)    (0x5200 + ((n) * 4)) // RX Multicast Table Array [0:127]
#define IE_RAL(n)    (0x5400 + ((n) * 8)) // RX Address Low
#define IE_RAH(n)    (0x5404 + ((n) * 8)) // RX Address High


#define IE_CTRL_FD        (1u << 0) // Full Duplex
#define IE_CTRL_LRST      (1u << 3) // Link Reset  (Halt TX and RX)
#define IE_CTRL_ASDE      (1u << 5) // Auto Speed Detect Enable
#define IE_CTRL_SLU       (1u << 6) // Set Link Up (ignored in ASDE mode)
#define IE_CTRL_ILOS      (1u << 7) // Invert Loss-of-Signal
#define IE_CTRL_SPEED     (3u << 8)
#define IE_CTRL_10M       (0u << 8)
#define IE_CTRL_100M      (1u << 8)
#define IE_CTRL_1000M     (2u << 8)
#define IE_CTRL_FRCSPD    (1u << 11) // Force Speed
#define IE_CTRL_RST       (1u << 26) // Device Reset (self-clearing after >1us)
#define IE_CTRL_VME       (1u << 30) // VLAN Mode Enable
#define IE_CTRL_PHY_RST   (1u << 31) // PHY Reset

#define IE_STATUS_FD      (1u << 0) // Full Duplex
#define IE_STATUS_LU      (1u << 1) // Link Up
#define IE_STATUS_TXOFF   (1u << 4)
#define IE_STATUS_TBIMODE (1u << 5)
#define IE_STATUS_SPEED   (3u << 6)
#define IE_STATUS_10M     (0u << 6)
#define IE_STATUS_100M    (1u << 6)
#define IE_STATUS_1000M   (2u << 6)

#define IE_MDIC_GET_DATA(val)    ((val) & 0xffff)
#define IE_MDIC_PUT_DATA(val)    ((val) & 0xffff)
#define IE_MDIC_GET_REGADD(val)  (((val) >> 16) & 0x1f)
#define IE_MDIC_PUT_REGADD(val)  (((val) & 0x1f) << 16)
#define IE_MDIC_GET_PHYADD(val)  (((val) >> 21) & 0x1f)
#define IE_MDIC_PUT_PHYADD(val)  (((val) & 0x1f) << 21)
#define IE_MDIC_OP_WRITE         (1u << 26)
#define IE_MDIC_OP_READ          (2u << 26)
#define IE_MDIC_R                (1u << 28)      // Ready
#define IE_MDIC_I                (1u << 29)      // Interrupt enable
#define IE_MDIC_E                (1u << 30)      // Error

#define IE_INT_TXDW       (1u << 0) // TX Descriptor Written Back
#define IE_INT_TXQE       (1u << 1) // TX Queue Empty
#define IE_INT_LSC        (1u << 2) // Link Status Change
#define IE_INT_RXSEQ      (1u << 3) // RX Sequence Error
#define IE_INT_RXDMT0     (1u << 4) // RX Descriptor Min Threshold
#define IE_INT_RXO        (1u << 6) // RX FIFO Overrun
#define IE_INT_RXT0       (1u << 7) // RX Timer
#define IE_INT_MDAC       (1u << 9) // MDIO Access Complete
#define IE_INT_PHYINT     (1u << 12 // PHY Interrupt

#define IE_RCTL_RST       (1u << 0) // RX Reset*
#define IE_RCTL_EN        (1u << 1) // RX Enable
#define IE_RCTL_SBP       (1u << 2) // Store Bad Packates
#define IE_RCTL_UPE       (1u << 3) // Unicast Promisc Enable
#define IE_RCTL_MPE       (1u << 4) // Multicast Promisc Enable
#define IE_RCTL_LPE       (1u << 5) // Long Packet RX Enable (>1522 bytes)
#define IE_RCTL_LBM       (3u << 6) // PHY/EXT Loopback
#define IE_RCTL_RDMTS2    (0u << 8) // RX Desc Min Thres 1/2 RDLEN
#define IE_RCTL_RDMTS4    (1u << 8) // RX Desc Min Thres 1/4 RDLEN
#define IE_RCTL_RDMTS8    (2u << 8) // RX Desc Min Thres 1/8 RDLEN
#define IE_RCTL_MO36      (0u << 12) // Multicast Filter Offset 36..47
#define IE_RCTL_MO35      (1u << 12) // Multicast Filter Offset 35..46
#define IE_RCTL_MO34      (2u << 12) // Multicast Filter Offset 34..45
#define IE_RCTL_MO32      (3u << 12) // Multicast Filter Offset 32..43
#define IE_RCTL_BAM       (1u << 15) // RX Broadcast Packets Enable
#define IE_RCTL_BSIZE2048 (0u << 16) // RX Buffer 2048 * (BSEX * 16)
#define IE_RCTL_BSIZE1024 (1u << 16) // RX Buffer 1024 * (BSEX * 16)
#define IE_RCTL_BSIZE512  (2u << 16) // RX Buffer 512 * (BSEX * 16)
#define IE_RCTL_BSIZE256  (3u << 16) // RX Buffer 256 * (BSEX * 16)
#define IE_RCTL_DPF       (1u << 22) // Discard Pause Frames
#define IE_RCTL_PMCF      (1u << 23) // Pass MAC Control Frames
#define IE_RCTL_BSEX      (1u << 25) // Buffer Size Extension (x16)
#define IE_RCTL_SECRC     (1u << 26) // Strip CRC Field

#define IE_TCTL_RESERVED  ((1u << 2) | (1u << 23) | (0xfu << 25) | (1u << 31))
#define IE_TCTL_RST       (1u << 0) // TX Reset?
#define IE_TCTL_EN        (1u << 1) // TX Enable
#define IE_TCTL_PSP       (1u << 3) // Pad Short Packets (to 64b)
#define IE_TCTL_CT(n)     ((n) << 4) // Collision Threshold (rec 15)
#define IE_TCTL_COLD_HD   (0x200u << 12) // Collision Distance Half Duplex
#define IE_TCTL_COLD_FD   (0x40u << 12) // Collision Distance Full Duplex
#define IE_TCTL_SWXOFF    (1u << 22) // XOFF TX (self-clearing)


typedef struct ie_rxd {
    uint64_t addr;
    uint64_t info;
} ie_rxd_t;

#define IE_RXD_RXE     (1ull << 47) // RX Data Error
#define IE_RXD_IPE     (1ull << 46) // IP Checksum Error
#define IE_RXD_TCPE    (1ull << 45) // TCP/UDP Checksum Error
#define IE_RXD_CXE     (1ull << 44) // Carrier Extension Error
#define IE_RXD_SEQ     (1ull << 42) // Sequence Error
#define IE_RXD_SE      (1ull << 41) // Symbol Error
#define IE_RXD_CE      (1ull << 40) // CRC Error or Alignment Error

#define IE_RXD_PIF     (1ull << 39) // Passed Inexact Filter
#define IE_RXD_IPCS    (1ull << 38) // IP Checksum Calculated
#define IE_RXD_TCPCS   (1ull << 37) // TCP Checksum Calculated
#define IE_RXD_VP      (1ull << 35) // 802.1Q / Matched VET
#define IE_RXD_IXSM    (1ull << 34) // Ignore IPCS and TCPCS bits
#define IE_RXD_EOP     (1ull << 33) // End of Packet (last desc)
#define IE_RXD_DONE    (1ull << 32) // Descriptor Done (hw is done)

#define IE_RXD_CHK(n)  (((n) >> 16) & 0xffff)
#define IE_RXD_LEN(n)  ((n) & 0xffff)

#define IE_RXDCTL_PTHRESH(n) (((uint32_t)(n) & 0x1f) <<  0)
#define IE_RXDCTL_HTHRESH(n) (((uint32_t)(n) & 0x1f) <<  8)
#define IE_RXDCTL_WTHRESH(n) (((uint32_t)(n) & 0x1f) << 16)
#define IE_RXDCTL_GRAN       (1u << 24)
#define IE_RXDCTL_ENABLE     (1u << 25)

typedef struct ie_txd {
    uint64_t addr;
    uint64_t info;
} ie_txd_t;

#define IE_TXD_TU     (1ull << 35) // TX Underrun
#define IE_TXD_LC     (1ull << 34) // Late Collision
#define IE_TXD_EC     (1ull << 33) // Excess Collisions
#define IE_TXD_DONE   (1ull << 32) // Descriptor Done

#define IE_TXD_IDE    (1ull << 31) // Interrupt Delay Enable
#define IE_TXD_VLE    (1ull << 30) // VLAN Packet Enable
#define IE_TXD_DEXT   (1ull << 29) // Extension
#define IE_TXD_RPS    (1ull << 28) // Report Packet Send
#define IE_TXD_RS     (1ull << 27) // Report Status
#define IE_TXD_IC     (1ull << 26) // Insert Checksum
#define IE_TXD_IFCS   (1ull << 25) // Insert FCS/CRC
#define IE_TXD_EOP    (1ull << 24) // End Of Packet

#define IE_TXD_CSS(n) ((((uint64_t)(n)) & 0xff) << 40)
#define IE_TXD_CSO(n) ((((uint64_t)(n)) & 0xff) << 16)
#define IE_TXD_LEN(n) (((uint64_t)(n)) & 0xffff)

#define IE_TXDCTL_WTHRESH(n) (((uint32_t)(n) & 0x1f) << 16)
#define IE_TXDCTL_GRAN       (1u << 24)
#define IE_TXDCTL_ENABLE     (1u << 25)

#define IE_MAX_PHY_ADDR               0x1f

// PHY registers

// PHY Control Register
#define IE_PHY_PCTRL                  (0x00)
#define IE_PHY_PCTRL_MASK             ((1u << 6) | (1u << 13))
#define IE_PHY_PCTRL_SPEED_1000       ((1u << 6) | (0u << 13))
#define IE_PHY_PCTRL_SPEED_100        ((0u << 6) | (1u << 13))
#define IE_PHY_PCTRL_SPEED_10         ((0u << 6) | (0u << 13))
#define IE_PHY_PCTRL_EN_COLL_TEST     (1u << 7)
#define IE_PHY_PCTRL_FULL_DUPLEX      (1u << 8)
#define IE_PHY_PCTRL_RESTART_AUTONEG  (1u << 9)
#define IE_PHY_PCTRL_ISOLATE          (1u << 10)
#define IE_PHY_PCTRL_POWER_DOWN       (1u << 11)
#define IE_PHY_PCTRL_EN_AUTONEG       (1u << 12)
#define IE_PHY_PCTRL_EN_LOOPBACK      (1u << 14)
#define IE_PHY_PCTRL_RESET            (1u << 15)

// PHY Identifier Register (LSB)
#define IE_PHY_PID                    (0x02)

// I211 registers
// Reference: Intel® Ethernet Controller I211 Datasheet
// June 2018
// Revision Number: 3.3
// Order No. 333017-006
//
// https://www.intel.com/content/www/us/en/embedded/products/networking/ethernet-controller-i210-i211-family-documentation.html
// https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/i211-ethernet-controller-datasheet.pdf?asset=9567

#define IE_IAM              0x00e0  // Interrupt Acknowledge Auto Mask Register
#define IE_EEC              0x12010 // EEPROM/Flash Control

#define IE_TCTL_BST(n)          (((n) & 0x3ff) << 12) // Back-Off Slot Time. This value determines
                                                      // the back-off slot time value in byte time.
#define IE_STATUS_PF_RST_DONE   (1u << 21)            // When set, indicates that software reset
                                                      // (CTRL.RST) or device reset (CTRL.DEV_RST)
                                                      // has completed and the software device
                                                      // driver can begin initialization process.
#define IE_INT_RXDW             (1u << 7)             // Receiver Descriptor Write Back. Set when
                                                      // the I211 writes back an Rx descriptor to
                                                      // memory.
#define IE_EEC_AUTO_RD          (1u << 9)
