// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Macros used for common register manipulation operations */
#define REG_MOD(reg_width, reg_base, reg_name, mask, val)   \
    pcie_write##reg_width(&reg_base->reg_name,              \
                         (pcie_read##reg_width(&reg_base->reg_name) \
                            & ((uint##reg_width##_t)(mask))) \
                            | ((uint##reg_width##_t)(val)));
#define REG_SET_BITS(reg_width, reg_base, reg_name, bits) \
        REG_MOD(reg_width, reg_base, reg_name, ~bits, bits)
#define REG_CLR_BITS(reg_width, reg_base, reg_name, bits) \
        REG_MOD(reg_width, reg_base, reg_name, ~bits, 0)
#define REG_RD(reg_width, reg_base, reg_name) pcie_read##reg_width(&reg_base->reg_name)
#define REG_WR(reg_width, reg_base, reg_name, val) pcie_write##reg_width(&reg_base->reg_name, val)
#define REG_RD_ADDR(reg_width, reg_addr) pcie_read##reg_width(reg_addr)
#define REG_WR_ADDR(reg_width, reg_addr, val) pcie_write##reg_width(reg_addr, val)

/**
 * Register definitions taken from
 *
 * Intel High Definition Audio Specification
 * Revision 1.0a
 * June 17, 2010
 *
 * http://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf
 */

typedef struct hda_stream_desc_regs {
    uint8_t  ctl[3];        // (0x00) Input Stream Descriptor 0 Control
    uint8_t  sts;           // (0x03) SD_n Status
    uint32_t lpib;          // (0x04) SD_n Link Position in Current Buffer
    uint32_t cbl;           // (0x08) SD_n Cyclic Buffer Length
    uint16_t lvi;           // (0x0C) SD_n Last Valid Index
    uint8_t  __rsvd0[2];    // (0x8E) Reserved
    uint16_t fifod;         // (0x10) SD_n FIFO Size
    uint16_t fmt;           // (0x12) SD_n Format
    uint8_t  __rsvd1[4];    // (0x14) Reserved
    uint32_t bdpl;          // (0x18) SD_n Buffer Descriptor List Pointer - Lower
    uint32_t bdpu;          // (0x1C) SD_n Buffer Descriptor List Pointer - Upper
} __PACKED hda_stream_desc_regs_t;

typedef struct hda_registers {
    uint16_t gcap;          // (0x00) Global Capabilities
    uint8_t  vmin;          // (0x02) Minor Version
    uint8_t  vmaj;          // (0x03) Major Version
    uint16_t outpay;        // (0x04) Output Payload Capability
    uint16_t inpay;         // (0x06) Input Payload Capability
    uint32_t gctl;          // (0x08) Global Control
    uint16_t wakeen;        // (0x0C) Wake Enable
    uint16_t statests;      // (0x0E) State Change Status
    uint16_t gsts;          // (0x10) Global Status
    uint8_t  __rsvd0[6];    // (0x12) Reserved
    uint16_t outstrmpay;    // (0x18) Output Stream Payload Capability
    uint16_t instrmpay;     // (0x1A) Input Stream Payload Capability
    uint8_t  __rsvd1[4];    // (0x1C) Reserved
    uint32_t intctl;        // (0x20) Interrupt Control
    uint32_t intsts;        // (0x24) Interrupt Status
    uint8_t  __rsvd2[8];    // (0x28) Reserved
    uint32_t walclk;        // (0x30) Wall Clock Counter
    uint8_t  __rsvd3[4];    // (0x34) Reserved
    uint32_t ssync;         // (0x38) Stream Synchronization
    uint8_t  __rsvd4[4];    // (0x3C) Reserved
    uint32_t corblbase;     // (0x40) CORB Lower Base Address
    uint32_t corbubase;     // (0x44) CORB Upper Base Address
    uint16_t corbwp;        // (0x48) CORB Write Pointer
    uint16_t corbrp;        // (0x4A) CORB Read Pointer
    uint8_t  corbctl;       // (0x4C) CORB Control
    uint8_t  corbsts;       // (0x4D) CORB Status
    uint8_t  corbsize;      // (0x4E) CORB Size
    uint8_t  __rsvd5[1];    // (0x4F) Reserved
    uint32_t rirblbase;     // (0x50) RIRB Lower Base Address
    uint32_t rirbubase;     // (0x54) RIRB Upper Base Address
    uint16_t rirbwp;        // (0x58) RIRB Write Pointer
    uint16_t rintcnt;       // (0x5A) Response Interrupt Count
    uint8_t  rirbctl;       // (0x5C) RIRB Control
    uint8_t  rirbsts;       // (0x5D) RIRB Status
    uint8_t  rirbsize;      // (0x5E) RIRB Size
    uint8_t  __rsvd6[1];    // (0x5F) Reserved
    uint32_t icoi;          // (0x60) Immediate Command Output Interface
    uint32_t icii;          // (0x64) Immediate Command Input Interface
    uint16_t icis;          // (0x68) Immediate Command Status
    uint8_t  __rsvd7[6];    // (0x6A) Reserved
    uint32_t dpiblbase;     // (0x70) DMA Position Buffer Lower Base
    uint32_t dpibubase;     // (0x74) DMA Position Buffer Upper Base
    uint8_t  __rsvd8[8];    // (0x78) Reserved

    // A max of 30 streams may be present in the system at any point in time (no
    // more than 15 input and 15 output).  The stream descriptor registers start
    // at 0x80, and are layed out as Input, then Output, then Bidirectional.
    // The number of each type of stream present in the hardware can be
    // determined using the GCAP register.
    hda_stream_desc_regs_t stream_desc[30]; // (0x80)
    uint8_t __rsvd9[0x1BC0];                // (0x440 - 0x1FFF)
} __PACKED hda_registers_t;

typedef struct hda_stream_desc_alias_regs {
    uint8_t  __rsvd0[0x04]; // (0x00) Reserved
    uint32_t lpib;          // (0x04) SD_n Link Position in Current Buffer Alias
    uint8_t  __rsvd1[0x18]; // (0x08) Reserved
} __PACKED hda_stream_desc_alias_regs_t;

typedef struct hda_alias_registers {
    uint8_t  __rsvd0[0x30];     // (0x00) Reserved
    uint32_t wallclk;           // (0x30) Wall Clock Counter Alias
    uint8_t  __rsvd1[0x4C];     // (0x34) Reserved
    hda_stream_desc_alias_regs_t stream_desc[30]; // (0x80)
    uint8_t __rsvd9[0x1BC0];    // (0x440 - 0x1FFF)
} __PACKED hda_alias_registers_t;

typedef struct hda_all_registers {
    hda_registers_t         regs;
    hda_alias_registers_t   alias_regs;
} __PACKED hda_all_registers_t;

/* Structs and constants for the command ring buffers */
typedef struct hda_corb_entry {
    uint32_t command;
} __PACKED hda_corb_entry_t;

#define HDA_CORB_MAX_ENTRIES (256u)
#define HDA_CORB_MAX_BYTES   (HDA_CORB_MAX_ENTRIES * sizeof(hda_corb_entry_t))

typedef struct hda_rirb_entry {  // See Table 54
    uint32_t data;
    uint32_t data_ex;
} __PACKED hda_rirb_entry_t;

#define HDA_RIRB_MAX_ENTRIES        (256u)
#define HDA_RIRB_MAX_BYTES          (HDA_RIRB_MAX_ENTRIES * sizeof(hda_rirb_entry_t))
#define HDA_RIRB_CADDR(resp)        (((resp).data_ex) & 0xF)
#define HDA_RIRB_UNSOL(resp)        ((((resp).data_ex) & 0x10) != 0)

/**
 * Bitfield definitions for various registers
 */

/* Global Capabilities Reigster (GCAP - offset 0x00) */
#define HDA_REG_GCAP_64OK           ((uint16_t)0x0001)
#define HDA_REG_GCAP_NSDO(val)      (((val) >>  1) & 0x03)
#define HDA_REG_GCAP_BSS(val)       (((val) >>  3) & 0x1F)
#define HDA_REG_GCAP_ISS(val)       (((val) >>  8) & 0x0F)
#define HDA_REG_GCAP_OSS(val)       (((val) >> 12) & 0x0F)

/* Global Control Reigster (GCTL - offset 0x08) */
#define HDA_REG_GCTL_HWINIT         ((uint32_t)0x0001)
#define HDA_REG_GCTL_FCNTRL         ((uint32_t)0x0002)
#define HDA_REG_GCTL_UNSOL          ((uint32_t)0x0100)

/* Interrupt Control Register (INTCTL - offset 0x20) */
#define HDA_REG_INTCTL_GIE          ((uint32_t)0x80000000)
#define HDA_REG_INTCTL_CIE          ((uint32_t)0x40000000)
#define HDA_REG_INTCTL_SIE(n)       (((uint32_t)0x1 << n) & 0x3FFFFFFF)

/* Command Output Ring Buffer Read Ptr (CORBRP - offset 0x4a) */
#define HDA_REG_CORBRP_RST          ((uint16_t)0x1000)

/* Command Output Ring Buffer Control (CORBCTL - offset 0x4c) */
#define HDA_REG_CORBCTL_MEIE        ((uint8_t)0x01)  // Mem Error Int Enable
#define HDA_REG_CORBCTL_DMA_EN      ((uint8_t)0x02)  // DMA Enable

/* Command Output Ring Buffer Status (CORBSTS - offset 0x4d) */
#define HDA_REG_CORBSTS_MEI         ((uint8_t)0x01)  // Memory Error Indicator

/* Command Output Ring Buffer Size (CORBSIZE - offset 0x4e) */
#define HDA_REG_CORBSIZE_CFG_2ENT   ((uint8_t)0x00)
#define HDA_REG_CORBSIZE_CFG_16ENT  ((uint8_t)0x01)
#define HDA_REG_CORBSIZE_CFG_256ENT ((uint8_t)0x02)
#define HDA_REG_CORBSIZE_CAP_2ENT   ((uint8_t)0x10)
#define HDA_REG_CORBSIZE_CAP_16ENT  ((uint8_t)0x20)
#define HDA_REG_CORBSIZE_CAP_256ENT ((uint8_t)0x40)

/* Response Input Ring Buffer Write Ptr (RIRBWP - offset 0x58) */
#define HDA_REG_RIRBWP_RST          ((uint16_t)0x1000)

/* Response Input Ring Buffer Control (RIRBCTL - offset 0x5c) */
#define HDA_REG_RIRBCTL_INTCTL      ((uint8_t)0x01)  // Interrupt Control
#define HDA_REG_RIRBCTL_DMA_EN      ((uint8_t)0x02)  // DMA Enable
#define HDA_REG_RIRBCTL_OIC         ((uint8_t)0x04)  // Overrun Interrupt Control

/* Response Input Ring Buffer Status (RIRBSTS - offset 0x5d) */
#define HDA_REG_RIRBSTS_INTFL       ((uint8_t)0x01)  // Response Interrupt Flag
#define HDA_REG_RIRBSTS_OIS         ((uint8_t)0x04)  // Overrun Interrupt Status

/* Response Input Ring Buffer Size (RIRBSIZE - offset 0x5e) */
#define HDA_REG_RIRBSIZE_CFG_2ENT   ((uint8_t)0x00)
#define HDA_REG_RIRBSIZE_CFG_16ENT  ((uint8_t)0x01)
#define HDA_REG_RIRBSIZE_CFG_256ENT ((uint8_t)0x02)
#define HDA_REG_RIRBSIZE_CAP_2ENT   ((uint8_t)0x10)
#define HDA_REG_RIRBSIZE_CAP_16ENT  ((uint8_t)0x20)
#define HDA_REG_RIRBSIZE_CAP_256ENT ((uint8_t)0x40)

__END_CDECLS
