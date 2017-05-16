// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>

#define AHCI_MAX_PORTS    32
#define AHCI_MAX_COMMANDS 32
#define AHCI_MAX_PRDS     8192 // for 32M max xfer size for fully discontiguous iotxn,
                               // hardware max is 64k-1

#define AHCI_PRD_MAX_SIZE 0x400000 // 4mb

#define AHCI_PORT_INT_CPD (1 << 31)
#define AHCI_PORT_INT_TFE (1 << 30)
#define AHCI_PORT_INT_HBF (1 << 29)
#define AHCI_PORT_INT_HBD (1 << 28)
#define AHCI_PORT_INT_IF  (1 << 27)
#define AHCI_PORT_INT_INF (1 << 26)
#define AHCI_PORT_INT_OF  (1 << 24)
#define AHCI_PORT_INT_IPM (1 << 23)
#define AHCI_PORT_INT_PRC (1 << 22)
#define AHCI_PORT_INT_DI  (1 << 7)
#define AHCI_PORT_INT_PC  (1 << 6)
#define AHCI_PORT_INT_DP  (1 << 5)
#define AHCI_PORT_INT_UF  (1 << 4)
#define AHCI_PORT_INT_SDB (1 << 3)
#define AHCI_PORT_INT_DS  (1 << 2)
#define AHCI_PORT_INT_PS  (1 << 1)
#define AHCI_PORT_INT_DHR (1 << 0)

#define AHCI_PORT_INT_ERROR (AHCI_PORT_INT_TFE | AHCI_PORT_INT_HBF | AHCI_PORT_INT_HBD | \
                             AHCI_PORT_INT_IF | AHCI_PORT_INT_INF | AHCI_PORT_INT_OF | \
                             AHCI_PORT_INT_IPM | AHCI_PORT_INT_PRC | AHCI_PORT_INT_PC | \
                             AHCI_PORT_INT_UF)
#define AHCI_PORT_INT_MASK (AHCI_PORT_INT_ERROR | AHCI_PORT_INT_DP | AHCI_PORT_INT_SDB | \
                            AHCI_PORT_INT_DS | AHCI_PORT_INT_PS | AHCI_PORT_INT_DHR)

#define AHCI_PORT_CMD_ST         (1 << 0)
#define AHCI_PORT_CMD_SUD        (1 << 1)
#define AHCI_PORT_CMD_POD        (1 << 2)
#define AHCI_PORT_CMD_FRE        (1 << 4)
#define AHCI_PORT_CMD_FR         (1 << 14)
#define AHCI_PORT_CMD_CR         (1 << 15)
#define AHCI_PORT_CMD_ATAPI      (1 << 24)
#define AHCI_PORT_CMD_ICC_ACTIVE (1 << 28)
#define AHCI_PORT_CMD_ICC_MASK   (0xf << 28)

#define AHCI_PORT_TFD_DATA_REQUEST (1 << 3)
#define AHCI_PORT_TFD_BUSY         (1 << 7)

#define AHCI_PORT_SIG_SATA 0x101

#define AHCI_PORT_SSTS_DET_PRESENT 3

#define AHCI_PORT_SCTL_IPM_ACTIVE  (1 << 8)
#define AHCI_PORT_SCTL_IPM_PARTIAL (2 << 8)
#define AHCI_PORT_SCTL_DET_MASK    0xf
#define AHCI_PORT_SCTL_DET_INIT    1

typedef struct {
    uint32_t clb;           // command list base address 1024-byte aligned
    uint32_t clbu;          // command list base address upper 32 bits
    uint32_t fb;            // FIS base address 256-byte aligned
    uint32_t fbu;           // FIS base address upper 32 bits
    uint32_t is;            // interrupt status
    uint32_t ie;            // interrupt enable
    uint32_t cmd;           // command and status
    uint32_t reserved0;     // reserved
    uint32_t tfd;           // task file data
    uint32_t sig;           // signature
    uint32_t ssts;          // SATA status
    uint32_t sctl;          // SATA control
    uint32_t serr;          // SATA error
    uint32_t sact;          // SATA active
    uint32_t ci;            // command issue
    uint32_t sntf;          // SATA notification
    uint32_t fbs;           // FIS-based switching control
    uint32_t devslp;        // device sleep
    uint32_t reserved1[10]; // reserved
    uint32_t vendor[4];     // vendor specific
} __attribute__((packed)) ahci_port_reg_t;

#define AHCI_CAP_NCQ (1 << 30)
#define AHCI_GHC_HR  (1 << 0)
#define AHCI_GHC_IE  (1 << 1)
#define AHCI_GHC_AE  (1 << 31)

typedef struct {
    uint32_t cap;              // host capabilities
    uint32_t ghc;              // global host control
    uint32_t is;               // interrupt status
    uint32_t pi;               // ports implemented
    uint32_t vs;               // version
    uint32_t ccc_ctl;          // command completion coalescing control
    uint32_t ccc_ports;        // command completion coalescing ports
    uint32_t em_loc;           // enclosure management location
    uint32_t em_ctl;           // enclosure management control
    uint32_t cap2;             // host capabilities extended
    uint32_t bohc;             // BIOS/OS handoff control and status
    uint32_t reserved[29];     // reserved
    uint32_t vendor[24];       // vendor specific registers
    ahci_port_reg_t ports[32]; // port control registers
} __attribute__((packed)) ahci_hba_t;

typedef struct {
    union {
        struct {
            uint16_t cfl : 5; // command FIS length
            uint16_t a : 1;   // ATAPI
            uint16_t w : 1;   // write
            uint16_t p : 1;   // prefetchable
            uint16_t r : 1;   // reset
            uint16_t b : 1;   // build in self test
            uint16_t c : 1;   // clear busy upon R_OK
            uint16_t rsvd : 1;
            uint16_t pmp : 4; // port multiplier port
            uint16_t prdtl;   // PRDT length
        } __attribute__((packed));
        uint32_t prdtl_flags_cfl;
    } __attribute__((packed));
    uint32_t prdbc;           // PRD byte count
    uint32_t ctba;            // command table base address 128-byte aligned
    uint32_t ctbau;           // command table base address upper 32 bits
    uint32_t reserved[4];     // reserved
} __attribute__((packed)) ahci_cl_t;

typedef struct {
    uint8_t dsfis[0x1c];     // DMA setup FIS
    uint8_t reserved1[0x4];
    uint8_t psfis[0x14];     // PIO setup FIS
    uint8_t reserved2[0x0c];
    uint8_t rfis[0x14];      // D2H register FIS
    uint8_t reserved3[0x4];
    uint8_t sdbfis[0x8];     // set device bits FIS
    uint8_t ufis[0x40];      // unknown FIS
    uint8_t reserved4[0x60];
} __attribute__((packed)) ahci_fis_t;

typedef struct {
    uint8_t cfis[0x40];            // command FIS
    uint8_t acmd[0x20];            // ATAPI command
    uint8_t reserved[0x20];        // reserved
} __attribute__((packed)) ahci_ct_t;

typedef struct {
    uint32_t dba;      // data base address 2-byte aligned
    uint32_t dbau;     // data base address upper 32 bits
    uint32_t reserved; // reserved
    uint32_t dbc;      // byte count max 4mb
} __attribute__((packed)) ahci_prd_t;

static_assert(sizeof(ahci_cl_t) == 0x20, "unexpected command list size");
static_assert(sizeof(ahci_fis_t) == 0x100, "unexpected fis size");
static_assert(sizeof(ahci_ct_t) == 0x80, "unexpected command table header size");
static_assert(sizeof(ahci_prd_t) == 0x10, "unexpected prd entry size");
