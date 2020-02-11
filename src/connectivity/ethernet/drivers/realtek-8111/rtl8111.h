// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

// TODO(stevensd): Handle errors (rx/tx/checksum/...)
// TODO(stevensd): Add support for VLAN tagging
// TODO(stevensd): Add support for multicast filtering
// TODO(stevensd): Surface hardware support for checksums to the netstack
// TODO(stevensd): Support jumbo packet and large send

// A ton of realtek controllers match this driver's VID/DID. This driver has only been tested
// on the 8111h rev2 (i.e. 0x54100000), but it should work (or mostly work) with any model of
// rtl8111, rtl8168, or rtl8411.

#define REALTEK_VID 0x10ec
#define RTL8111_DID 0x8168 // Also the rtl8168 and rtl8411

#define ETH_BUF_SIZE ROUNDDOWN(1522, 8) // maximum ethernet frame, rtl8111 requires 8 byte size
#define ETH_BUF_COUNT 64
#define ETH_DESC_ELT_SIZE 16
#define ETH_DESC_RING_SIZE (ETH_DESC_ELT_SIZE * ETH_BUF_COUNT)

#define RTL_MAC0 0x0000
#define RTL_MAC1 0x0004
#define RTL_MAR7 0x0008
#define RTL_MAR3 0x000c
#define RTL_TNPDS_LOW 0x0020
#define RTL_TNPDS_HIGH 0x0024
#define RTL_CR 0x0037
#define RTL_TPPOLL 0x0038
#define RTL_IMR 0x003c
#define RTL_ISR 0x003e
#define RTL_TCR 0x0040
#define RTL_RCR 0x0044
#define RTL_9436CR 0x0050
#define RTL_PHYSTATUS 0x006c
#define RTL_RMS 0x00da
#define RTL_CPLUSCR 0x00e0
#define RTL_RDSAR_LOW 0x00e4
#define RTL_RDSAR_HIGH 0x00e8
#define RTL_MTPS 0x00ec

#define RTL_CR_RST (1 << 4)
#define RTL_CR_RE (1 << 3)
#define RTL_CR_TE (1 << 2)

#define RTL_TPPOLL_NPQ (1 << 6)

#define RTL_INT_MASK ((1 << 14) | 0x3ff)
#define RTL_INT_LINKCHG (1 << 5)
#define RTL_INT_TOK (1 << 2)
#define RTL_INT_ROK (1 << 0)

#define RTL_TCR_IFG_MASK ((3 << 24) | 1 << 19)
#define RTL_TCR_IFG96 ((3 << 24) | 0)
#define RTL_TCR_MXDMA_MASK (7 << 8)
#define RTL_TCR_MXDMA_UNLIMITED (7 << 8)

#define RTL_RCR_MXDMA_MASK (7 << 8)
#define RTL_RCR_MXDMA_UNLIMITED (7 << 8)
#define RTL_RCR_ACCEPT_MASK (RTL_RCR_AB | RTL_RCR_AM | RTL_RCR_APM | RTL_RCR_AAP)
#define RTL_RCR_AB (1 << 3)
#define RTL_RCR_AM (1 << 2)
#define RTL_RCR_APM (1 << 1)
#define RTL_RCR_AAP (1 << 0)

#define RTL_9436CR_EEM_MASK (3 << 6)
#define RTL_9436CR_EEM_LOCK (0 << 6)
#define RTL_9436CR_EEM_UNLOCK (3 << 6)

#define RTL_PHYSTATUS_LINKSTS (1 << 1)

#define RTL_RMS_RMS_MASK 0x3fff

#define RTL_CPLUSCR_RXVLAN (1 << 6)
#define RTL_CPLUSCR_RXCHKSUM (1 << 5)

#define RTL_MTPS_MTPS_MASK 0x1f

#define TX_DESC_OWN (1 << 31)
#define TX_DESC_EOR (1 << 30)
#define TX_DESC_FS (1 << 29)
#define TX_DESC_LS (1 << 28)
#define TX_DESC_LEN_MASK 0xffff

#define RX_DESC_OWN (1 << 31)
#define RX_DESC_EOR (1 << 30)
#define RX_DESC_LEN_MASK 0x3fff
