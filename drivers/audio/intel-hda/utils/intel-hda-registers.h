// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <fbl/type_support.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Register definitions taken from
 *
 * Intel High Definition Audio Specification
 * Revision 1.0a
 * June 17, 2010
 *
 * http://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf
 */

// TODO(johngro)
//
// Consider moving these structures and their associated flags into a more C++
// style, where the flags can either be static constexpr members of the
// structures, or in their own namespace.  Also, consider moving away from the
// underscore separated typedef declarations, and to LeadingUpperCamelCased
// declarations made without typedef and the _t suffix.
//
// Currently, we have to keep the register structures C compatible, because they
// are used in the response for the CONTROLLER_SNAPSHOT_REGS command.  When we
// can send back a read-only VMO to provide debug access to the registers, we
// can do away with this.
typedef struct hda_stream_desc_regs {
    // (0x00) Control (0x03) Status
    union {
        uint32_t w;         // 32-bit word access to the Control/Status registers
        struct {
            uint8_t ctl[3]; // Control register byte access
            uint8_t sts;    // Status register byte access
        } b;
    } ctl_sts;
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

#ifdef __cplusplus

namespace audio {
namespace intel_hda {

/**
 * Bitfield definitions for various registers
 */

/* Global Capabilities Reigster (GCAP - offset 0x00) */
#define _SIC_ static inline constexpr

_SIC_ bool     HDA_REG_GCAP_64OK(uint16_t val) { return (val & 1u) != 0; }
_SIC_ uint16_t HDA_REG_GCAP_NSDO(uint16_t val) { return (val >>  1) & 0x03; }
_SIC_ uint16_t HDA_REG_GCAP_BSS (uint16_t val) { return (val >>  3) & 0x1F; }
_SIC_ uint16_t HDA_REG_GCAP_ISS (uint16_t val) { return (val >>  8) & 0x0F; }
_SIC_ uint16_t HDA_REG_GCAP_OSS (uint16_t val) { return (val >> 12) & 0x0F; }

/* Global Control Reigster (GCTL - offset 0x08) */
constexpr uint32_t HDA_REG_GCTL_HWINIT = 0x0001u;
constexpr uint32_t HDA_REG_GCTL_FCNTRL = 0x0002u;
constexpr uint32_t HDA_REG_GCTL_UNSOL  = 0x0100u;

/* Wake Enable and State Status (WAKEEN/STATESTS - offsets 0x0C,0x0E) */
static constexpr uint16_t HDA_REG_STATESTS_MASK = 0x7FFFu;

/* Interrupt Control Register (INTCTL - offset 0x20) */
constexpr uint32_t HDA_REG_INTCTL_GIE      = 0x80000000u;
constexpr uint32_t HDA_REG_INTCTL_CIE      = 0x40000000u;
constexpr uint32_t HDA_REG_INTCTL_SIE_MASK = 0x3FFFFFFFu;
_SIC_ uint32_t HDA_REG_INTCTL_SIE(unsigned int n) { return (0x1u << n) & HDA_REG_INTCTL_SIE_MASK; }

/* Command Output Ring Buffer Read Ptr (CORBRP - offset 0x4a) */
constexpr uint16_t HDA_REG_CORBRP_RST = 0x8000u;

/* Command Output Ring Buffer Control (CORBCTL - offset 0x4c) */
constexpr uint8_t HDA_REG_CORBCTL_MEIE   = 0x01u;  // Mem Error Int Enable
constexpr uint8_t HDA_REG_CORBCTL_DMA_EN = 0x02u;  // DMA Enable

/* Command Output Ring Buffer Status (CORBSTS - offset 0x4d) */
constexpr uint8_t HDA_REG_CORBSTS_MEI = 0x01u;  // Memory Error Indicator

/* Command Output Ring Buffer Size (CORBSIZE - offset 0x4e) */
constexpr uint8_t HDA_REG_CORBSIZE_CFG_2ENT   = 0x00u;
constexpr uint8_t HDA_REG_CORBSIZE_CFG_16ENT  = 0x01u;
constexpr uint8_t HDA_REG_CORBSIZE_CFG_256ENT = 0x02u;
constexpr uint8_t HDA_REG_CORBSIZE_CAP_2ENT   = 0x10u;
constexpr uint8_t HDA_REG_CORBSIZE_CAP_16ENT  = 0x20u;
constexpr uint8_t HDA_REG_CORBSIZE_CAP_256ENT = 0x40u;

/* Response Input Ring Buffer Write Ptr (RIRBWP - offset 0x58) */
constexpr uint16_t HDA_REG_RIRBWP_RST = 0x1000u;

/* Response Input Ring Buffer Control (RIRBCTL - offset 0x5c) */
constexpr uint8_t HDA_REG_RIRBCTL_INTCTL = 0x01u;  // Interrupt Control
constexpr uint8_t HDA_REG_RIRBCTL_DMA_EN = 0x02u;  // DMA Enable
constexpr uint8_t HDA_REG_RIRBCTL_OIC    = 0x04u;  // Overrun Interrupt Control

/* Response Input Ring Buffer Status (RIRBSTS - offset 0x5d) */
constexpr uint8_t HDA_REG_RIRBSTS_INTFL = 0x01u;  // Response Interrupt Flag
constexpr uint8_t HDA_REG_RIRBSTS_OIS   = 0x04u;  // Overrun Interrupt Status

/* Response Input Ring Buffer Size (RIRBSIZE - offset 0x5e) */
constexpr uint8_t HDA_REG_RIRBSIZE_CFG_2ENT   = 0x00u;
constexpr uint8_t HDA_REG_RIRBSIZE_CFG_16ENT  = 0x01u;
constexpr uint8_t HDA_REG_RIRBSIZE_CFG_256ENT = 0x02u;
constexpr uint8_t HDA_REG_RIRBSIZE_CAP_2ENT   = 0x10u;
constexpr uint8_t HDA_REG_RIRBSIZE_CAP_16ENT  = 0x20u;
constexpr uint8_t HDA_REG_RIRBSIZE_CAP_256ENT = 0x40u;

// Stream Descriptor Control Register bits.
constexpr uint32_t HDA_SD_REG_CTRL_SRST    = (1u << 0); // Stream Reset
constexpr uint32_t HDA_SD_REG_CTRL_RUN     = (1u << 1); // Stream Run
constexpr uint32_t HDA_SD_REG_CTRL_IOCE    = (1u << 2); // Interrupt on Completion Enable
constexpr uint32_t HDA_SD_REG_CTRL_FEIE    = (1u << 3); // FIFO Error Interrupt Enable
constexpr uint32_t HDA_SD_REG_CTRL_DEIE    = (1u << 4); // Descripto Error Interrupt Enable
constexpr uint32_t HDA_SD_REG_CTRL_STRIPE1 = (0u << 16); // 1 SDO line
constexpr uint32_t HDA_SD_REG_CTRL_STRIPE2 = (1u << 16); // 2 SDO lines
constexpr uint32_t HDA_SD_REG_CTRL_STRIPE4 = (2u << 16); // 4 SDO lines
constexpr uint32_t HDA_SD_REG_CTRL_TP      = (1u << 18); // Traffic Priority
constexpr uint32_t HDA_SD_REG_CTRL_DIR_IN  = (0u << 19); // Direction Control
constexpr uint32_t HDA_SD_REG_CTRL_DIR_OUT = (1u << 19); // Direction Control
_SIC_ uint32_t HDA_SD_REG_CTRL_STRM_TAG(uint8_t tag) {   // Stream Tag
    return static_cast<uint32_t>(tag & 0xF) << 20;
}

// Stream Descriptor Status Register bits arranged for both 8 and 32 bit access.
constexpr uint8_t HDA_SD_REG_STS8_BCIS     = (1u << 2); // Buffer Complete IRQ Status
constexpr uint8_t HDA_SD_REG_STS8_FIFOE    = (1u << 3); // FIFO error IRQ Status
constexpr uint8_t HDA_SD_REG_STS8_DESE     = (1u << 4); // Descriptor Error IRQ Status
constexpr uint8_t HDA_SD_REG_STS8_FIFORDY  = (1u << 5); // FIFO ready
constexpr uint8_t HDA_SD_REG_STS8_ACK      = HDA_SD_REG_STS8_BCIS  |
                                             HDA_SD_REG_STS8_FIFOE |
                                             HDA_SD_REG_STS8_DESE;
constexpr uint8_t HDA_SD_REG_STS8_MASK     = HDA_SD_REG_STS8_ACK |
                                             HDA_SD_REG_STS8_FIFORDY;

constexpr uint32_t HDA_SD_REG_STS32_BCIS     = static_cast<uint32_t>(HDA_SD_REG_STS8_BCIS)    << 24;
constexpr uint32_t HDA_SD_REG_STS32_FIFOE    = static_cast<uint32_t>(HDA_SD_REG_STS8_FIFOE)   << 24;
constexpr uint32_t HDA_SD_REG_STS32_DESE     = static_cast<uint32_t>(HDA_SD_REG_STS8_DESE)    << 24;
constexpr uint32_t HDA_SD_REG_STS32_FIFORDY  = static_cast<uint32_t>(HDA_SD_REG_STS8_FIFORDY) << 24;
constexpr uint32_t HDA_SD_REG_STS32_ACK      = static_cast<uint32_t>(HDA_SD_REG_STS8_ACK)     << 24;
constexpr uint32_t HDA_SD_REG_STS32_MASK     = static_cast<uint32_t>(HDA_SD_REG_STS8_MASK)    << 24;

// Stream Descriptor Status Register bits.

#undef _SIC_

// The definition of a Buffer Descriptor List entry.  See Section 3.6.3 and
// Table 50.
//
// TODO(johngro) : Figure out what the endianness requirements are for these
// structures.  Intel specs the memory mapped controller registers as being
// strictly little endian, but they do not go seem into details about in memory
// control structures (like the CORB/RIRB, packed samples, BDL entries, etc..)
//
// For now, I assume that they are expecting little endian (just like they are
// for their registers).  Also, this will probably not be an issue until the day
// that there is a big endian system with an Intel HDA controller embedded in it
// (which does not seem likely, any time soon).
struct IntelHDABDLEntry {
    uint64_t    address;
    uint32_t    length;
    uint32_t    flags;

    // Interrupt-on-complete flag.
    static constexpr uint32_t IOC_FLAG = 0x1u;
} __PACKED;

// TODO(johngro) : Intel specs its controller registers as little endian.
// Someday, we should update these template/macros to deal with conversion
// to/from host endian instead of assuming a little endian host.
template <typename T>
static inline T REG_RD(const T* reg) {
    return *(reinterpret_cast<const volatile T*>(reg));
}

template <typename T, typename U>
static inline void REG_WR(T* reg, U val) {
    static_assert(fbl::is_unsigned_integer<T>::value, "");
    ZX_DEBUG_ASSERT(static_cast<T>(-1) >= val);
    *(reinterpret_cast<volatile T*>(reg)) = static_cast<T>(val);
}

template <typename T>
static inline void REG_MOD(T* reg, T clr_bits, T set_bits) {
    REG_WR(reg, static_cast<T>((REG_RD(reg) & ~clr_bits) | set_bits));
}

template <typename T> static inline void REG_SET_BITS(T* reg, T bits) {
    REG_MOD(reg, static_cast<T>(0), bits);
}

template <typename T> static inline void REG_CLR_BITS(T* reg, T bits) {
    REG_MOD(reg, bits, static_cast<T>(0));
}

}  // namespace intel_hda
}  // namespace audio

#endif  // __cplusplus

