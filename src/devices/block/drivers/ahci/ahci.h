// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_AHCI_AHCI_H_
#define SRC_DEVICES_BLOCK_DRIVERS_AHCI_AHCI_H_

#include <limits.h>
#include <stdint.h>

// For the purposes of calculating other constants we expect this to be the system page size. This
// is validated at run time prior to binding.
#define AHCI_PAGE_SIZE 4096

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_COMMANDS 32
#define AHCI_MAX_PRDS ((AHCI_PAGE_SIZE / sizeof(zx_paddr_t)) + 1)
#define AHCI_MAX_PAGES AHCI_MAX_PRDS
// one page less of 2M because of unaligned offset
#define AHCI_MAX_BYTES (2 * 1024 * 1024)

#define AHCI_PRD_MAX_SIZE 0x400000  // 4mb
static_assert(AHCI_PAGE_SIZE <= AHCI_PRD_MAX_SIZE, "page size must be less than PRD max size\n");

#define AHCI_PORT_INT_CPD (1u << 31)  // Cold Port Detect Status.
#define AHCI_PORT_INT_TFE (1u << 30)  // Task File Error status.
#define AHCI_PORT_INT_HBF (1u << 29)  // Host Bus Fatal Error Status.
#define AHCI_PORT_INT_HBD (1u << 28)  // Host Bus Data Error Status.
#define AHCI_PORT_INT_IF (1u << 27)   // Interface Fatal Error Status.
#define AHCI_PORT_INT_INF \
  (1u << 26)                          // Interface Non-fatal Error Status.
                                      // Reserved
#define AHCI_PORT_INT_OF (1u << 24)   // Overflow Status.
#define AHCI_PORT_INT_IPM (1u << 23)  // Incorrect Port Multiplier Status.
#define AHCI_PORT_INT_PRC \
  (1u << 22)                         // PhyRdy Change Status.
                                     // Reserved
#define AHCI_PORT_INT_DI (1u << 7)   // Device Mechanical Presence Status.
#define AHCI_PORT_INT_PC (1u << 6)   // Port Connect Change Status.
#define AHCI_PORT_INT_DP (1u << 5)   // Descriptor Processed.
#define AHCI_PORT_INT_UF (1u << 4)   // Unknown FIS Interrupt.
#define AHCI_PORT_INT_SDB (1u << 3)  // Set Device Bits Interrupt.
#define AHCI_PORT_INT_DS (1u << 2)   // DMA Setup FIS Interrupt.
#define AHCI_PORT_INT_PS (1u << 1)   // PIO Setup FIS Interrupt.
#define AHCI_PORT_INT_DHR (1u << 0)  // Device to Host Register FIS Interrupt.

#define AHCI_PORT_INT_ERROR                                                       \
  (AHCI_PORT_INT_TFE | AHCI_PORT_INT_HBF | AHCI_PORT_INT_HBD | AHCI_PORT_INT_IF | \
   AHCI_PORT_INT_INF | AHCI_PORT_INT_OF | AHCI_PORT_INT_IPM | AHCI_PORT_INT_PRC | \
   AHCI_PORT_INT_PC | AHCI_PORT_INT_UF)
#define AHCI_PORT_INT_MASK                                                         \
  (AHCI_PORT_INT_ERROR | AHCI_PORT_INT_DP | AHCI_PORT_INT_SDB | AHCI_PORT_INT_DS | \
   AHCI_PORT_INT_PS | AHCI_PORT_INT_DHR)

#define AHCI_PORT_CMD_ST (1u << 0)
#define AHCI_PORT_CMD_SUD (1u << 1)
#define AHCI_PORT_CMD_POD (1u << 2)
#define AHCI_PORT_CMD_FRE (1u << 4)
#define AHCI_PORT_CMD_FR (1u << 14)
#define AHCI_PORT_CMD_CR (1u << 15)
#define AHCI_PORT_CMD_ATAPI (1u << 24)
#define AHCI_PORT_CMD_ICC_ACTIVE (1u << 28)
#define AHCI_PORT_CMD_ICC_MASK (0xf << 28)

#define AHCI_PORT_TFD_DATA_REQUEST (1u << 3)
#define AHCI_PORT_TFD_BUSY (1u << 7)

#define AHCI_PORT_SIG_SATA 0x101

#define AHCI_PORT_SSTS_DET_PRESENT 3

#define AHCI_PORT_SCTL_IPM_ACTIVE (1u << 8)
#define AHCI_PORT_SCTL_IPM_PARTIAL (2u << 8)
#define AHCI_PORT_SCTL_DET_MASK 0xf
#define AHCI_PORT_SCTL_DET_INIT 1

namespace ahci {

struct ahci_port_reg_t {
  uint32_t clb;            // command list base address 1024-byte aligned
  uint32_t clbu;           // command list base address upper 32 bits
  uint32_t fb;             // FIS base address 256-byte aligned
  uint32_t fbu;            // FIS base address upper 32 bits
  uint32_t is;             // interrupt status
  uint32_t ie;             // interrupt enable
  uint32_t cmd;            // command and status
  uint32_t reserved0;      // reserved
  uint32_t tfd;            // task file data
  uint32_t sig;            // signature
  uint32_t ssts;           // SATA status
  uint32_t sctl;           // SATA control
  uint32_t serr;           // SATA error
  uint32_t sact;           // SATA active
  uint32_t ci;             // command issue
  uint32_t sntf;           // SATA notification
  uint32_t fbs;            // FIS-based switching control
  uint32_t devslp;         // device sleep
  uint32_t reserved1[10];  // reserved
  uint32_t vendor[4];      // vendor specific
} __attribute__((packed));

constexpr size_t kPortCommandListBase = offsetof(ahci_port_reg_t, clb);
constexpr size_t kPortCommandListBaseUpper = offsetof(ahci_port_reg_t, clbu);
constexpr size_t kPortFISBase = offsetof(ahci_port_reg_t, fb);
constexpr size_t kPortFISBaseUpper = offsetof(ahci_port_reg_t, fbu);
constexpr size_t kPortInterruptStatus = offsetof(ahci_port_reg_t, is);
constexpr size_t kPortInterruptEnable = offsetof(ahci_port_reg_t, ie);
constexpr size_t kPortCommand = offsetof(ahci_port_reg_t, cmd);
constexpr size_t kPortTaskFileData = offsetof(ahci_port_reg_t, tfd);
constexpr size_t kPortSignature = offsetof(ahci_port_reg_t, sig);
constexpr size_t kPortSataStatus = offsetof(ahci_port_reg_t, ssts);
constexpr size_t kPortSataControl = offsetof(ahci_port_reg_t, sctl);
constexpr size_t kPortSataError = offsetof(ahci_port_reg_t, serr);
constexpr size_t kPortSataActive = offsetof(ahci_port_reg_t, sact);
constexpr size_t kPortCommandIssue = offsetof(ahci_port_reg_t, ci);
constexpr size_t kPortSataNotification = offsetof(ahci_port_reg_t, sntf);
constexpr size_t kPortFisBasedSwitching = offsetof(ahci_port_reg_t, fbs);
constexpr size_t kPortDeviceSleep = offsetof(ahci_port_reg_t, devslp);

#define AHCI_CAP_NCQ (1u << 30)
#define AHCI_GHC_HR (1u << 0)
#define AHCI_GHC_IE (1u << 1)
#define AHCI_GHC_AE (1u << 31)

struct ahci_hba_t {
  uint32_t cap;               // host capabilities
  uint32_t ghc;               // global host control
  uint32_t is;                // interrupt status
  uint32_t pi;                // ports implemented
  uint32_t vs;                // version
  uint32_t ccc_ctl;           // command completion coalescing control
  uint32_t ccc_ports;         // command completion coalescing ports
  uint32_t em_loc;            // enclosure management location
  uint32_t em_ctl;            // enclosure management control
  uint32_t cap2;              // host capabilities extended
  uint32_t bohc;              // BIOS/OS handoff control and status
  uint32_t reserved[29];      // reserved
  uint32_t vendor[24];        // vendor specific registers
  ahci_port_reg_t ports[32];  // port control registers
} __attribute__((packed));

constexpr size_t kHbaCapabilities = offsetof(ahci_hba_t, cap);
constexpr size_t kHbaGlobalHostControl = offsetof(ahci_hba_t, ghc);
constexpr size_t kHbaInterruptStatus = offsetof(ahci_hba_t, is);
constexpr size_t kHbaPortsImplemented = offsetof(ahci_hba_t, pi);
constexpr size_t kHbaVersion = offsetof(ahci_hba_t, vs);
constexpr size_t kHbaCoalescingControl = offsetof(ahci_hba_t, ccc_ctl);
constexpr size_t kHbaCoalescingPorts = offsetof(ahci_hba_t, ccc_ports);
constexpr size_t kHbaEnclosureLocation = offsetof(ahci_hba_t, em_loc);
constexpr size_t kHbaEnclosureControl = offsetof(ahci_hba_t, em_ctl);
constexpr size_t kHbaCapabilitiesExtended = offsetof(ahci_hba_t, cap2);
constexpr size_t kHbaBiosHandoffControl = offsetof(ahci_hba_t, bohc);
constexpr size_t kHbaVendor = offsetof(ahci_hba_t, vendor);
constexpr size_t kHbaPorts = offsetof(ahci_hba_t, ports);

// Command List.
struct ahci_cl_t {
  union {
    struct {
      uint16_t cfl : 5;  // command FIS length
      uint16_t a : 1;    // ATAPI
      uint16_t w : 1;    // write
      uint16_t p : 1;    // prefetchable
      uint16_t r : 1;    // reset
      uint16_t b : 1;    // build in self test
      uint16_t c : 1;    // clear busy upon R_OK
      uint16_t rsvd : 1;
      uint16_t pmp : 4;  // port multiplier port
      uint16_t prdtl;    // PRDT length
    } __attribute__((packed));
    uint32_t prdtl_flags_cfl;
  } __attribute__((packed));
  uint32_t prdbc;        // PRD byte count
  uint32_t ctba;         // command table base address 128-byte aligned
  uint32_t ctbau;        // command table base address upper 32 bits
  uint32_t reserved[4];  // reserved
} __attribute__((packed));

// Frame Information Structure.
struct ahci_fis_t {
  uint8_t dsfis[0x1c];  // DMA setup FIS
  uint8_t reserved1[0x4];
  uint8_t psfis[0x14];  // PIO setup FIS
  uint8_t reserved2[0x0c];
  uint8_t rfis[0x14];  // D2H register FIS
  uint8_t reserved3[0x4];
  uint8_t sdbfis[0x8];  // set device bits FIS
  uint8_t ufis[0x40];   // unknown FIS
  uint8_t reserved4[0x60];
} __attribute__((packed));

// Command Table Header.
struct ahci_ct_t {
  uint8_t cfis[0x40];      // command FIS
  uint8_t acmd[0x20];      // ATAPI command
  uint8_t reserved[0x20];  // reserved
} __attribute__((packed));

// Physical Region Descriptor Entry.
struct ahci_prd_t {
  uint32_t dba;       // data base address 2-byte aligned
  uint32_t dbau;      // data base address upper 32 bits
  uint32_t reserved;  // reserved
  uint32_t dbc;       // byte count max 4mb
} __attribute__((packed));

static_assert(sizeof(ahci_cl_t) == 0x20, "unexpected command list size");
static_assert(sizeof(ahci_fis_t) == 0x100, "unexpected fis size");
static_assert(sizeof(ahci_ct_t) == 0x80, "unexpected command table header size");
static_assert(sizeof(ahci_prd_t) == 0x10, "unexpected prd entry size");

}  // namespace ahci

#endif  // SRC_DEVICES_BLOCK_DRIVERS_AHCI_AHCI_H_
