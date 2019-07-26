// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define MII_BMCR 0x00        /* Basic mode control register */
#define MII_BMSR 0x01        /* Basic mode status register  */
#define MII_PHYSID1 0x02     /* PHYS ID 1               */
#define MII_PHYSID2 0x03     /* PHYS ID 2               */
#define MII_ADVERTISE 0x04   /* Advertisement control reg   */
#define MII_LPA 0x05         /* Link partner ability reg    */
#define MII_EXPANSION 0x06   /* Expansion register           */
#define MII_GBCR 0x09        /* 1000BASE-T control           */
#define MII_GBSR 0x0a        /* 1000BASE-T status           */
#define MII_ESTATUS 0x0f     /* Extended Status */
#define MII_DCOUNTER 0x12    /* Disconnect counter           */
#define MII_FCSCOUNTER 0x13  /* False carrier counter       */
#define MII_NWAYTEST 0x14    /* N-way auto-neg test reg     */
#define MII_RERRCOUNTER 0x15 /* Receive error counter       */
#define MII_SREVISION 0x16   /* Silicon revision           */
#define MII_RESV1 0x17       /* Reserved...               */
#define MII_LBRERROR 0x18    /* Lpback, rx, bypass error    */
#define MII_PHYADDR 0x19     /* PHY address               */
#define MII_RESV2 0x1a       /* Reserved...               */
#define MII_TPISTATUS 0x1b   /* TPI status for 10mbps       */
#define MII_NCONFIG 0x1c     /* Network interface config    */
#define MII_EPAGSR 0x1f      /* Page Select register */

/* Basic mode control register. */
#define BMCR_RESV 0x003f      /* Unused...                   */
#define BMCR_SPEED1000 0x0040 /* MSB of Speed (1000)         */
#define BMCR_CTST 0x0080      /* Collision test              */
#define BMCR_FULLDPLX 0x0100  /* Full duplex                 */
#define BMCR_ANRESTART 0x0200 /* Auto negotiation restart    */
#define BMCR_ISOLATE 0x0400   /* Isolate data paths from MII */
#define BMCR_PDOWN 0x0800     /* Enable low power state      */
#define BMCR_ANENABLE 0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100 0x2000  /* Select 100Mbps              */
#define BMCR_LOOPBACK 0x4000  /* TXD loopback bits           */
#define BMCR_RESET 0x8000     /* Reset to default state      */
#define BMCR_SPEED10 0x0000   /* Select 10Mbps               */

/* Basic mode status register. */
#define BMSR_ERCAP 0x0001        /* Ext-reg capability          */
#define BMSR_JCD 0x0002          /* Jabber detected             */
#define BMSR_LSTATUS 0x0004      /* Link status                 */
#define BMSR_ANEGCAPABLE 0x0008  /* Able to do auto-negotiation */
#define BMSR_RFAULT 0x0010       /* Remote fault detected       */
#define BMSR_ANEGCOMPLETE 0x0020 /* Auto-negotiation complete   */
#define BMSR_RESV 0x00c0         /* Unused...                   */
#define BMSR_ESTATEN 0x0100      /* Extended Status in R15      */
#define BMSR_100HALF2 0x0200     /* Can do 100BASE-T2 HDX       */
#define BMSR_100FULL2 0x0400     /* Can do 100BASE-T2 FDX       */
#define BMSR_10HALF 0x0800       /* Can do 10mbps, half-duplex  */
#define BMSR_10FULL 0x1000       /* Can do 10mbps, full-duplex  */
#define BMSR_100HALF 0x2000      /* Can do 100mbps, half-duplex */
#define BMSR_100FULL 0x4000      /* Can do 100mbps, full-duplex */
#define BMSR_100BASE4 0x8000     /* Can do 100mbps, 4k packets  */
