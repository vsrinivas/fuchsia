// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

// Registers in PCIE BAR0 MMIO Space
#define NVME_REG_CAP              0x00 // Capabilities
#define NVME_REG_VS               0x08 // Version
#define NVME_REG_INTMS            0x0C // Interrupt Mask Set
#define NVME_REG_INTMC            0x10 // Interrupt Mask clear
#define NVME_REG_CC               0x14 // Controller Configuration
#define NVME_REG_CSTS             0x1C // Controller Status
#define NVME_REG_NSSR             0x20 // NVM Subsystem Reset (Optional)
#define NVME_REG_AQA              0x24 // Admin Queue Attributes
#define NVME_REG_ASQ              0x28 // Admin Submission Queue Base Addr
#define NVME_REG_ACQ              0x30 // Admin Completion Queue Base Addr
#define NVME_REG_CMBLOC           0x38 // Controller Memory Block Location (Optional)
#define NVME_REG_CMBSZ            0x3C // Controller Memory Block Size (Optional)

// Submission/Completion Queue Tail/Head are computed based on capabilities
#define NVME_REG_SQnTDBL(n, cap)  (0x1000 + (2*(n) + 0) * (4 << (NVME_CAP_DSTRD(cap) - 2)))
#define NVME_REG_CQnHDBL(n, cap)  (0x1000 + (2*(n) + 1) * (4 << (NVME_CAP_DSTRD(cap) - 2)))

#define NVME_CAP_MPSMAX(n)        ((((n) >> 52) & 0xF) + 12)   // 2^x bytes
#define NVME_CAP_MPSMIN(n)        ((((n) >> 48) & 0xF) + 12)   // 2^x bytes
#define NVME_CAP_BPS(n)           (((n) >> 45) & 1)
#define NVME_CAP_CSS_NVM(n)       (((n) >> 37) & 1)
#define NVME_CAP_NSSRS(n)         (((n) >> 36) & 1)
#define NVME_CAP_DSTRD(n)         ((((n) >> 32) & 0xF) + 2)    // 2^x bytes
#define NVME_CAP_TO(n)            ((((n) >> 24) & 0xFF) * 500)   // milliseconds
#define NVME_CAP_AMS_WRR(n)       (((n) >> 17) & 1)
#define NVME_CAP_AMS_VS(n)        (((n) >> 18) & 1)
#define NVME_CAP_CQR(n)           (((n) >> 16) & 1)
#define NVME_CAP_MQES(n)          ((n) & 0xFFFF)

#define NVME_CC_IOCQES(n)         (((n) & 0xF) << 20) // IO Completion Entry Size 2^n
#define NVME_CC_IOSQES(n)         (((n) & 0xF) << 16) // IO Submission Entry Size 2^n
#define NVME_CC_SHN_NORMAL        (1 << 14) // Request Normal Shutdown
#define NVME_CC_SHN_ABRUPT        (2 << 14) // Request Abrupt Shutdown
#define NVME_CC_SHN_MASK          (3 << 14)
#define NVME_CC_AMS_RR            (0 << 11) // Arbitration: Round-Robin
#define NVME_CC_AMS_WRR           (1 << 11) // Arbitration: Weighted-Round-Robin
#define NVME_CC_AMS_VS            (7 << 11) // Arbitration: Vendor Specific
#define NVME_CC_MPS(n)            (((n) & 0xF) << 7) // Memory Page Size (2^(n + 12))
#define NVME_CC_EN                (1 << 0) // Enable

#define NVME_CSTS_PP              (1 << 5)  // Processing Paused
#define NVME_CSTS_NSSRO           (1 << 4)  // Subsystem Reset Occurred (W1C)
#define NVME_CSTS_SHN_MASK        (3 << 2)
#define NVME_CSTS_SHN_NORMAL_OP   (0 << 2)  // not shutting done
#define NVME_CSTS_SHN_IN_PROGRESS (1 << 2)  // Shutdown is in progress
#define NVME_CSTS_SHN_COMPLETE    (2 << 2)  // Shutdown is complete
#define NVME_CSTS_CFS             (1 << 1)  // Controller Fatal Status
#define NVME_CSTS_RDY             (1 << 0)  // Ready

#define NVME_AQA_ACQS(n)          (((n) & 0xFFF) << 16) // Admin Completion Queue Size
#define NVME_AQA_ASQS(n)          (((n) & 0xFFF) << 0)  // Admin Submission Queue Size


// Completion Queue Entry
typedef struct {
    uint32_t cmd;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cmd_id;
    uint16_t status;
} nvme_cpl_t;

#define NVME_CPL_SIZE 16
#define NVME_CPL_SHIFT 4
static_assert(sizeof(nvme_cpl_t) == NVME_CPL_SIZE, "");
static_assert(sizeof(nvme_cpl_t) == (1 << NVME_CPL_SHIFT), "");

#define NVME_CPL_STATUS_CODE(n) (((n) >> 1) & 0x7FF)


// Submission Queue Entry
typedef struct {
    uint32_t cmd;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    union {
        uint64_t prp[2];
    } dptr;
    union {
        uint32_t raw[6];
        struct {
            uint64_t start_lba;
            uint16_t block_count; // minus 1
            uint16_t flags;
            uint32_t dsm;
            uint32_t eilbrt;
            uint32_t elbat;
        } rw;
    } u;
} nvme_cmd_t;

#define NVME_CMD_SIZE 64
#define NVME_CMD_SHIFT 6
static_assert(sizeof(nvme_cmd_t) == NVME_CMD_SIZE, "");
static_assert(sizeof(nvme_cmd_t) == (1 << NVME_CMD_SHIFT), "");

// Common
#define NVME_CMD_CID(n)    (((n) & 0xFFFF) << 16)

#define NVME_CMD_PRP       (0 << 14) // dptr uses PRP, mptr is raw addr
#define NVME_CMD_SGL       (1 << 14) // dptr uses SGL, mptr is raw addr
#define NVME_CMD_SGL_MSGL  (2 << 14) // dptr uses SGL, mptr points at SGL[1]

#define NVME_CMD_NORMAL    (0 << 8) // non-fused command
#define NVME_CMD_FUSED_1ST (1 << 8) // 1st part of fused command
#define NVME_CMD_FUSED_2ND (2 << 8) // 2nd part of fused command

#define NVME_CMD_OPC(n)    ((n) & 0xFF)



// Admin Opcodes
#define NVME_ADMIN_OP_DELETE_IOSQ   0x00
#define NVME_ADMIN_OP_CREATE_IOSQ   0x01
#define NVME_ADMIN_OP_DELETE_IOCQ   0x04
#define NVME_ADMIN_OP_CREATE_IOCQ   0x05
#define NVME_ADMIN_OP_IDENTIFY      0x06
#define NVME_ADMIN_OP_ABORT         0x08
#define NVME_ADMIN_OP_SET_FEATURE   0x09
#define NVME_ADMIN_OP_GET_FEATURE   0x0A
#define NVME_ADMIN_OP_ASYNC_EVENT   0x0C

#define NVME_FEATURE_SEL_CURRENT   (0 << 8)
#define NVME_FEATURE_SEL_DEFAULT   (1 << 8)
#define NVME_FEATURE_SEL_SAVED     (2 << 8)
#define NVME_FEATURE_SEL_SUPPORTED (3 << 8)

#define NVME_FEATURE_NUMBER_OF_QUEUES 0x07


#define NVME_LBAFMT_RP(n)      (((n) >> 24) & 3)
#define NVME_LBAFMT_LBADS(n)   (((n) >> 16) & 0xFF)  // 2^n bytes
#define NVME_LBAFMT_MS(n)      ((n) & 0xFFFF)

// NVM Opcodes
#define NVME_OP_FLUSH       0x00
#define NVME_OP_WRITE       0x01
#define NVME_OP_READ        0x02

#define NVME_RW_FLAG_LR     (1 << 15)
#define NVME_RW_FLAG_FUA    (1 << 14)


// Identify Page for Controllers
typedef struct {
    uint32_t w[8];
} nvme_psd_t;

typedef struct {
    //--------------------- // Controller Capabilities and Features
    uint16_t VID;           // PCI Vendor ID
    uint16_t SSVID;         // PCI Subsystem Vendor ID
    uint8_t  SN[20];        // Serial Number
    uint8_t  MN[40];        // Model Number
    uint8_t  FR[8];         // Firmware Revision
    uint8_t  RAB;           // Recommended Arbitrartion Burst
    uint8_t  IEEE[3];       // IEEE OUI Identifier
    uint8_t  CMIC;          // Controller Multi-Path IO and Namespace Sharing Caps
    uint8_t  MDTS;          // Maximum Data Transfer Size
    uint16_t CNTLID;        // Controller ID
    uint32_t VER;           // Version
    uint32_t RTD3R;         // RTD3 Resume Latency (uS)
    uint32_t RTD3E;         // RTD3 ENtry Latency (uS)
    uint32_t OAES;          // Optional Asynch Events Supported;
    uint32_t CTRATT;        // Controller Attributes
    uint8_t  zz0[12];       // Reserved
    uint8_t  FGUID[16];     // Field Replaceable Unit GUID
    uint8_t  zz1[112];      // Reserved
    uint8_t  zz2[16];       // Refer to NVMe MI Spec

    // -------------------- // Admin Command Set Attributes and Capabilities
    uint16_t OACS;          // Optional Admin Command Support
    uint8_t  ACL;           // Abort Command Limit
    uint8_t  AERL;          // Async Event Request Limit
    uint8_t  FRMW;          // Firmware Updates
    uint8_t  LPA;           // Log Page Attributes;
    uint8_t  ELPE;          // Error Log Page Entries
    uint8_t  NPSS;          // Number of Power States Supported
    uint8_t  AVSCC;         // Admin Vendor Specific Command Config
    uint8_t  APSTA;         // Autonomous Power State Transition Attrs
    uint16_t WCTEMP;        // Warning Composite Temp Threshold
    uint16_t CCTEMP;        // Critical Composite Temp Threshold
    uint16_t MTFA;          // Max Time for Firmware Activation (x 100mS, 0 = undef)
    uint32_t HMPRE;         // Host Memory Buffer Preferred Size (4K pages)
    uint32_t HMMIN;         // Host Memory Buffer Minimum Size (4K pages)
    uint64_t TNVMCAP_LO;    // Total NVM Capacity (bytes)
    uint64_t TNVMCAP_HI;
    uint64_t UNVMCAP_LO;    // Unallocated NVM Capacity (bytes)
    uint64_t UNVMCAP_HI;
    uint32_t RPMBS;         // Replay Protected Memory Block Support
    uint16_t EDSTT;         // Extended Device SelfTest Time
    uint8_t  DSTO;          // Devcie SelfTest Options
    uint8_t  FWUG;          // Firmware Upgreade Granularity
    uint16_t KAS;           // Keep Alive Support
    uint16_t HCTMA;         // Host Controlled Thermal Management Attrs
    uint16_t MNTMT;         // Minimum Thermal Management Temp
    uint16_t MXTMT;         // Maximum Thermal Management Temp
    uint32_t SANICAP;       // Sanitize Capabilities
    uint8_t  zz3[180];      // Reserved

    // -------------------- // NVM Command Set Attributes
    uint8_t  SQES;          // Submission Queue Entry Size
    uint8_t  CQES;          // Completion Queue Entry Size
    uint16_t MAXCMD;        // Max Outstanding Commands
    uint32_t NN;            // Number of Namespaces
    uint16_t ONCS;          // Optional NVM Command Support
    uint16_t FUSES;         // Fused Operation Support
    uint8_t  FNA;           // Format NVM Attributes
    uint8_t  VWC;           // Volatile Write Cache
    uint16_t AWUN;          // Atomic Write Unit Normal
    uint16_t AWUPF;         // Atomic Write Unit Power Fail
    uint8_t  NVSCC;         // NVM Vendor Specific Command Config
    uint8_t  zz4;           // Reserved
    uint16_t ACWU;          // Atomic Compare and Write Unit
    uint16_t zz5;           // Reserved
    uint32_t SGLS;          // Scatter Gather List Support
    uint8_t  zz6[228];      // Reserved
    uint8_t  SUBNQN[256];   // NVM Subsystem NVMe Qualified Name
    uint8_t  zz7[768];      // Reserved
    uint8_t  zz8[256];      // Refer to NVME over Fabrics Spec

    // -------------------- // Power State Descriptors
    nvme_psd_t PSD[32];

    // -------------------- // Vendor Specific
    uint8_t  vendor[1024];
} nvme_identify_t;

static_assert(sizeof(nvme_identify_t) == 4096, "");

#define OACS_DOORBELL_BUFFER_CONFIG     (1 << 8)
#define OACS_VIRTUALIZATION_MANAGEMENT  (1 << 7)
#define OACS_NVME_MI_SEND_RECV          (1 << 6)
#define OACS_DIRECTIVE_SEND_RECV        (1 << 5)
#define OACS_DEVICE_SELF_TEST           (1 << 4)
#define OACS_NAMESPACE_MANAGEMENT       (1 << 3)
#define OACS_FIRMWARE_DOWNLOAD_COMMIT   (1 << 2)
#define OACS_FORMAT_NVM                 (1 << 1)
#define OACS_SECURITY_SEND_RECV         (1 << 0)

#define ONCS_TIMESTAMP                  (1 << 6)
#define ONCS_RESERVATIONS               (1 << 5)
#define ONCS_SAVE_SELECT_NONZERO        (1 << 4)
#define ONCS_WRITE_ZEROES               (1 << 3)
#define ONCS_DATASET_MANAGEMENT         (1 << 2)
#define ONCS_WRITE_UNCORRECTABLE        (1 << 1)
#define ONCS_COMPARE                    (1 << 0)


// Identify Page for Namespaces
#define NSFEAT_GUIDS_NOT_REUSED         (1 << 3)
#define NSFEAT_DEALLOC_BLOCK_ERROR      (1 << 2)
#define NSFEAT_LOCAL_ATOMIC_SIZES       (1 << 1)
#define NSFEAT_THING_PROVISIONING       (1 << 0)


typedef struct {
    // -------------------- // Vendor Specific
    uint64_t NSSZ;          // Namespace Size (blocks)
    uint64_t NCAP;          // Namespace Capacity (blocks)
    uint64_t NUSE;          // Namespace Utilization (blocks)
    uint8_t  NSFEAT;        // Namespace Features
    uint8_t  NLBAF;         // Number of LBA Formats
    uint8_t  FLBAS;         // Formatted LBA Size
    uint8_t  MC;            // Metadata Capabilities
    uint8_t  DPC;           // End-to-End Data Protection Capabilities
    uint8_t  DPS;           // End-to-End Data Protection Type Settings
    uint8_t  NMIC;          // Namespace MultiPath IO and Sharing Caps
    uint8_t  RESCAP;        // Reservation Capabilities
    uint8_t  FPI;           // Format Progress Indicator
    uint8_t  DLFEAT;        // Deallocate Logical Block Features
    uint16_t NAWUN;         // Namespace Atomic Write Unit Normal
    uint16_t NAWUPF;        // Namespace Atomic Write Unit Power Fail
    uint16_t NACWUN;        // Namespace Atomic Compare and Write Unit
    uint16_t NABSN;         // Namespace Atomic Boundary Size Normal
    uint16_t NABO;          // Namespace Atomic Boundary Offset
    uint16_t NABSPF;        // Namespace Atomic Boundary Size Power Fail
    uint16_t NOIOB;         // Namespace Optimal IO Boundary
    uint64_t NVMCAP_LO;     // NVM Capacity (bytes)
    uint64_t NVMCAP_HI;
    uint8_t  zz0[40];       // Reserved
    uint8_t  NGUID[16];     // Namespace GUID
    uint8_t  EUI64[8];      // IEEE Extended Unique Identifier
    uint32_t LBAF[16];      // LBA Format Support 0..15
    uint8_t  zz1[192];      // Reserved
    uint8_t  zz2[3712];     // Reserved
} nvme_identify_ns_t;

static_assert(sizeof(nvme_identify_ns_t) == 4096, "");

#define NSFEAT_GUIDS_NOT_REUSED         (1 << 3)
#define NSFEAT_DEALLOC_BLOCK_ERROR      (1 << 2)
#define NSFEAT_LOCAL_ATOMIC_SIZES       (1 << 1)
#define NSFEAT_THING_PROVISIONING       (1 << 0)
