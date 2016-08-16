// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// See: PCI/PCI-X Family of Gigabit Ethernet Controllers
//      Software Developer's Manual
//      317553006EN.PDF
//      Revision 4.0

#define IE_CTRL      0x0000 // Device Control
#define IE_STATUS    0x0008 // Device Status
#define IE_CTRL_EXT  0x0018 // Extended Device Control
#define IE_TXCW      0x0178 // TX Config Word
#define IE_RXCW      0x0180 // RX Config Word
#define IE_ICR       0x00C0 // Interrupt Cause Read
#define IE_ICS       0x00C8 // Interrupt Cause Set
#define IE_IMS       0x00D0 // Interrupt Mask Set / Read
#define IE_IMC       0x00D8 // Interrupt Mask Clear

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


#define IE_CTRL_FD        (1 << 0) // Full Duplex
#define IE_CTRL_LRST      (1 << 3) // Link Reset  (Halt TX and RX)
#define IE_CTRL_ASDE      (1 << 5) // Auto Speed Detect Enable
#define IE_CTRL_SLU       (1 << 6) // Set Link Up (ignored in ASDE mode)
#define IE_CTRL_ILOS      (1 << 7) // Invert Loss-of-Signal
#define IE_CTRL_SPEED     (3 << 8)
#define IE_CTRL_10M       (0 << 8)
#define IE_CTRL_100M      (1 << 8)
#define IE_CTRL_1000M     (2 << 8)
#define IE_CTRL_FRCSPD    (1 << 11) // Force Speed
#define IE_CTRL_RST       (1 << 26) // Device Reset (self-clearing after >1us)
#define IE_CTRL_VME       (1 << 30) // VLAN Mode Enable
#define IE_CTRL_PHY_RST   (1 << 31) // PHY Reset

#define IE_STATUS_FD      (1 << 0) // Full Duplex
#define IE_STATUS_LU      (1 << 1) // Link Up
#define IE_STATUS_TXOFF   (1 << 4)
#define IE_STATUS_TBIMODE (1 << 5)
#define IE_STATUS_SPEED   (3 << 6)
#define IE_STATUS_10M     (0 << 6)
#define IE_STATUS_100M    (1 << 6)
#define IE_STATUS_1000M   (2 << 6)

#define IE_INT_TXDW       (1 << 0) // TX Descriptor Written Back
#define IE_INT_TXQE       (1 << 1) // TX Queue Empty
#define IE_INT_LSC        (1 << 2) // Link Status Change
#define IE_INT_RXSEQ      (1 << 3) // RX Sequence Error
#define IE_INT_RXDMT0     (1 << 4) // RX Descriptor Min Threshold
#define IE_INT_RXO        (1 << 6) // RX FIFO Overrun
#define IE_INT_RXT0       (1 << 7) // RX Timer
#define IE_INT_MDAC       (1 << 9) // MDIO Access Complete
#define IE_INT_PHYINT     (1 << 12 // PHY Interrupt

#define IE_RCTL_RST       (1 << 0) // RX Reset*
#define IE_RCTL_EN        (1 << 1) // RX Enable
#define IE_RCTL_SBP       (1 << 2) // Store Bad Packates
#define IE_RCTL_UPE       (1 << 3) // Unicast Promisc Enable
#define IE_RCTL_MPE       (1 << 4) // Multicast Promisc Enable
#define IE_RCTL_LPE       (1 << 5) // Long Packet RX Enable (>1522 bytes)
#define IE_RCTL_LBM       (3 << 6) // PHY/EXT Loopback
#define IE_RCTL_RDMTS2    (0 << 8) // RX Desc Min Thres 1/2 RDLEN
#define IE_RCTL_RDMTS4    (1 << 8) // RX Desc Min Thres 1/4 RDLEN
#define IE_RCTL_RDMTS8    (2 << 8) // RX Desc Min Thres 1/8 RDLEN
#define IE_RCTL_MO36      (0 << 12) // Multicast Filter Offset 36..47
#define IE_RCTL_MO35      (1 << 12) // Multicast Filter Offset 35..46
#define IE_RCTL_MO34      (2 << 12) // Multicast Filter Offset 34..45
#define IE_RCTL_MO32      (3 << 12) // Multicast Filter Offset 32..43
#define IE_RCTL_BAM       (1 << 15) // RX Broadcast Packets Enable
#define IE_RCTL_BSIZE2048 (0 << 16) // RX Buffer 2048 * (BSEX * 16)
#define IE_RCTL_BSIZE1024 (1 << 16) // RX Buffer 1024 * (BSEX * 16)
#define IE_RCTL_BSIZE512  (2 << 16) // RX Buffer 512 * (BSEX * 16)
#define IE_RCTL_BSIZE256  (3 << 16) // RX Buffer 256 * (BSEX * 16)
#define IE_RCTL_DPF       (1 << 22) // Discard Pause Frames
#define IE_RCTL_PMCF      (1 << 23) // Pass MAC Control Frames
#define IE_RCTL_BSEX      (1 << 25) // Buffer Size Extension (x16)
#define IE_RCTL_SECRC     (1 << 26) // Strip CRC Field

#define IE_TCTL_RST       (1 << 0) // TX Reset?
#define IE_TCTL_EN        (1 << 1) // TX Enable
#define IE_TCTL_PSP       (1 << 3) // Pad Short Packets (to 64b)
#define IE_TCTL_CT(n)     ((n) << 4) // Collision Threshold (rec 15)
#define IE_TCTL_COLD_HD   (0x200 << 12) // Collision Distance Half Duplex
#define IE_TCTL_COLD_FD   (0x40 << 12) // Collision Distance Full Duplex
#define IE_TCTL_SWXOFF    (1 << 22) // XOFF TX (self-clearing)


typedef struct ie_rxd {
    uint64_t addr;
    uint64_t info;
} ie_rxd_t;

#define IE_RXD_RXE     (1ULL << 47) // RX Data Error
#define IE_RXD_IPE     (1ULL << 46) // IP Checksum Error
#define IE_RXD_TCPE    (1ULL << 45) // TCP/UDP Checksum Error
#define IE_RXD_CXE     (1ULL << 44) // Carrier Extension Error
#define IE_RXD_SEQ     (1ULL << 42) // Sequence Error
#define IE_RXD_SE      (1ULL << 41) // Symbol Error
#define IE_RXD_CE      (1ULL << 40) // CRC Error or Alignment Error

#define IE_RXD_PIF     (1ULL << 39) // Passed Inexact Filter
#define IE_RXD_IPCS    (1ULL << 38) // IP Checksum Calculated
#define IE_RXD_TCPCS   (1ULL << 37) // TCP Checksum Calculated
#define IE_RXD_VP      (1ULL << 35) // 802.1Q / Matched VET
#define IE_RXD_IXSM    (1ULL << 34) // Ignore IPCS and TCPCS bits
#define IE_RXD_EOP     (1ULL << 33) // End of Packet (last desc)
#define IE_RXD_DONE    (1ULL << 32) // Descriptor Done (hw is done)

#define IE_RXD_CHK(n)  (((n) >> 16) & 0xFFFF)
#define IE_RXD_LEN(n)  ((n) & 0xFFFF)


typedef struct ie_txd {
    uint64_t addr;
    uint64_t info;
} ie_txd_t;

#define IE_TXD_TU     (1ULL << 35) // TX Underrun
#define IE_TXD_LC     (1ULL << 34) // Late Collision
#define IE_TXD_EC     (1ULL << 33) // Excess Collisions
#define IE_TXD_DONE   (1ULL << 32) // Descriptor Done

#define IE_TXD_IDE    (1ULL << 31) // Interrupt Delay Enable
#define IE_TXD_VLE    (1ULL << 30) // VLAN Packet Enable
#define IE_TXD_DEXT   (1ULL << 29) // Extension
#define IE_TXD_RPS    (1ULL << 28) // Report Packet Send
#define IE_TXD_RS     (1ULL << 27) // Report Status
#define IE_TXD_IC     (1ULL << 26) // Insert Checksum
#define IE_TXD_IFCS   (1ULL << 25) // Insert FCS/CRC
#define IE_TXD_EOP    (1ULL << 24) // End Of Packet

#define IE_TXD_CSS(n) ((((uint64_t)(n)) & 0xFFULL) << 40)
#define IE_TXD_CSO(n) ((((uint64_t)(n)) & 0xFFULL) << 16)
#define IE_TXD_LEN(n) (((uint64_t)(n)) & 0xFFFFULL)


