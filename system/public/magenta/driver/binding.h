// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS;

// COAABBBB VVVVVVVV  Condition Opcode paramA paramB Value

#define OP_ABORT  0x0 // if (cond) return no-match
#define OP_MATCH  0x1 // if (cond) return match
#define OP_GOTO   0x2 // if (cond) advance to next LABEL(paramA)
#define OP_SET    0x3 // if (cond) flags |= paramA
#define OP_CLEAR  0x4 // if (cond) flags &= (~paramA)
#define OP_LABEL  0x5 // no-op, labels line with paramA

#define COND_AL   0x0 // true
#define COND_EQ   0x1 // bind(paramB) == Value
#define COND_NE   0x2 // bind(paramB) != Value
#define COND_GT   0x3 // bind(paramB) > Value
#define COND_LT   0x4 // bind(paramB) < Value
#define COND_GE   0x5 // bind(paramB) >= Value
#define COND_LE   0x6 // bind(paramB) <= Value
#define COND_MASK 0x7 // (bind(paramB) & Value) != 0
#define COND_BITS 0x8 // (bind(paramB) & Value) == Value

// branches are forward-only
// branches always go to the first matching LABEL
// branches that cannot find a matching LABEL are treated as ABORTs
// there is an implied unconditional ABORT after the last instruction
// flags are initially zero, may be set/cleared with SET/CLEAR
// flags may be tested by comparison against BIND_FLAGS

#define BINDINST(c,o,a,b,v) \
    { (((c)&0xF)<<28)|(((o)&0xF)<<24)|(((a)&0xFF)<<16)|((b)&0xFFFF),(v) }

#define BINDINST_CC(n) ((n) >> 28)
#define BINDINST_OP(n) (((n) >> 24) & 0xF)
#define BINDINST_PA(n) (((n) >> 16) & 0xFF)
#define BINDINST_PB(n) ((n) & 0xFFFF)

#define BI_ABORT()            BINDINST(COND_AL,OP_ABORT,0,0,0)
#define BI_MATCH()            BINDINST(COND_AL,OP_MATCH,0,0,0)
#define BI_GOTO(n)            BINDINST(COND_AL,OP_GOTO,n,0,0)
#define BI_SET(f)             BINDINST(COND_AL,OP_SET,f,0,0)
#define BI_CLEAR(f)           BINDINST(COND_AL,OP_CLEAR,f,0,0)
#define BI_LABEL(n)           BINDINST(COND_AL,OP_LABEL,n,0,0)

#define BI_ABORT_IF(c,b,v)    BINDINST(COND_##c,OP_ABORT,0,b,v)
#define BI_MATCH_IF(c,b,v)    BINDINST(COND_##c,OP_MATCH,0,b,v)
#define BI_GOTO_IF(c,b,v,n)   BINDINST(COND_##c,OP_GOTO,n,b,v)
#define BI_SET_IF(c,b,v,f)    BINDINST(COND_##c,OP_SET,f,b,v)
#define BI_CLEAR_IF(c,b,v,f)  BINDINST(COND_##c,OP_CLEAR,f,b,v)

// for drivers that only want to be bound on user request
#define BI_ABORT_IF_AUTOBIND  BI_ABORT_IF(NE, BIND_AUTOBIND, 0)

// global binding variables at 0x00XX
#define BIND_FLAGS            0x0000 // value of the flags register
#define BIND_PROTOCOL         0x0001 // primary protcol of the device
#define BIND_AUTOBIND         0x0002 // if this is an automated bind/load

// pci binding variables at 0x01XX
#define BIND_PCI_VID          0x0100
#define BIND_PCI_DID          0x0101
#define BIND_PCI_CLASS        0x0102
#define BIND_PCI_SUBCLASS     0x0103
#define BIND_PCI_INTERFACE    0x0104
#define BIND_PCI_REVISION     0x0105
#define BIND_PCI_BDF_ADDR     0x0106

// pci binding variable utils
#define BIND_PCI_BDF_PACK(bus, dev, func) \
    ((((uint32_t)(bus)  & 0xFF) << 8) |   \
     (((uint32_t)(dev)  & 0x1F) << 3) |   \
      ((uint32_t)(func) & 0x07))

#define BIND_PCI_BDF_UNPACK_BUS(bdf) (((uint32_t)(bdf) >> 8) & 0xFF)
#define BIND_PCI_BDF_UNPACK_DEV(bdf) (((uint32_t)(bdf) >> 3) & 0x1F)
#define BIND_PCI_BDF_UNPACK_FUNC(bdf) ((uint32_t)(bdf) & 0x07)

// usb binding variables at 0x02XX
// these are used for both MX_PROTOCOL_USB and MX_PROTOCOL_USB_FUNCTION
#define BIND_USB_VID          0x0200
#define BIND_USB_PID          0x0201
#define BIND_USB_CLASS        0x0202
#define BIND_USB_SUBCLASS     0x0203
#define BIND_USB_PROTOCOL     0x0204

// Platform device binding variables at 0x03XX
#define BIND_PLATFORM_DEV_VID 0x0300
#define BIND_PLATFORM_DEV_PID 0x0301
#define BIND_PLATFORM_DEV_DID 0x0302

// ACPI binding variables at 0x04XX
// The _HID is a 7- or 8-byte string. Because a bind property is 32-bit, use 2
// properties to bind using the _HID. They are encoded in big endian order for
// human readability. In the case of 7-byte _HID's, the 8th-byte shall be 0.
#define BIND_ACPI_HID_0_3      0x0400 // char 0-3
#define BIND_ACPI_HID_4_7      0x0401 // char 4-7
// The _CID may be a valid HID value or a bus-specific string. The ACPI bus
// driver only publishes those that are valid HID values.
#define BIND_ACPI_CID_0_3      0x0402 // char 0-3
#define BIND_ACPI_CID_4_7      0x0403 // char 4-7

// Intel HDA Codec binding variables at 0x05XX
#define BIND_IHDA_CODEC_VID         0x0500
#define BIND_IHDA_CODEC_DID         0x0501
#define BIND_IHDA_CODEC_MAJOR_REV   0x0502
#define BIND_IHDA_CODEC_MINOR_REV   0x0503
#define BIND_IHDA_CODEC_VENDOR_REV  0x0504
#define BIND_IHDA_CODEC_VENDOR_STEP 0x0505

// TEMPORARY binding variables at 0xfXX
// I2C_ADDR is a temporary way to bind the i2c touchscreen on the Acer12. This
// binding will eventually be made via some sort of ACPI device enumeration.
#define BIND_I2C_ADDR         0x0f00

typedef struct mx_bind_inst {
    uint32_t op;
    uint32_t arg;
} mx_bind_inst_t;

typedef struct mx_device_prop {
    uint16_t id;
    uint16_t reserved;
    uint32_t value;
} mx_device_prop_t;

// simple example
#if 0
mx_bind_inst_t i915_binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1616), // broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1916), // skylake
    BI_ABORT(),
};
#endif

#define MAGENTA_NOTE_NAME "Magenta"
#define MAGENTA_NOTE_DRIVER 0x31565244 // DRV1

typedef struct {
    // Elf64_Nhdr fields:
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    // ELF note name.  namesz is the exact size of the name (including '\0'),
    // but the storage size is always rounded up to a multiple of 4 bytes.
    char name[(sizeof(MAGENTA_NOTE_NAME) + 3) & -4];
} magenta_driver_note_header_t;

#define MAGENTA_DRIVER_NOTE_HEADER_INIT(object) {        \
        /* .namesz = */ sizeof(MAGENTA_NOTE_NAME),              \
        /* .descsz = */ (sizeof(object) -                       \
                         sizeof(magenta_driver_note_header_t)), \
        /* .type = */ MAGENTA_NOTE_DRIVER,                      \
        /* .name = */ MAGENTA_NOTE_NAME,                        \
    }

typedef struct {
    // Future Expansion
    uint32_t flags;

    // Driver Metadata
    uint32_t bindcount;
    uint32_t reserved0;
    char name[32];
    char vendor[16];
    char version[16];

    // Driver Bind Program follows
} magenta_driver_note_payload_t;

#define MAGENTA_DRIVER_NOTE_PAYLOAD_INIT(Driver,VendorName,Version,BindCount) \
    {                                                               \
        /* .flags = */ 0,                                           \
        /* .bindcount = */ (BindCount),                             \
        /* .reserved0 = */ 0,                                       \
        /* .name = */ #Driver,                                      \
        /* .vendor = */ VendorName,                                 \
        /* .version = */ Version,                                   \
    }

typedef struct {
    alignas(4) magenta_driver_note_header_t header;
    alignas(4) magenta_driver_note_payload_t payload;
} magenta_driver_note_t;

static_assert(offsetof(magenta_driver_note_t, payload) ==
              sizeof(magenta_driver_note_header_t),
              "alignment snafu?");

// Without this, ASan will add redzone padding after the object, which
// would make it invalid ELF note format.
#if __has_feature(address_sanitizer)
# define MAGENTA_DRIVER_NOTE_ASAN __attribute__((no_sanitize("address")))
#else
# define MAGENTA_DRIVER_NOTE_ASAN
#endif

// GCC has a quirk about how '__attribute__((visibility("default")))'
// (__EXPORT here) works for const variables in C++.  The attribute has no
// effect when used on the definition of a const variable, and GCC gives a
// warning/error about that.  The attribute must appear on the "extern"
// declaration of the variable instead.

#define MAGENTA_DRIVER_BEGIN(Driver,Ops,VendorName,Version,BindCount) \
mx_driver_rec_t __magenta_driver_rec__ __EXPORT = {\
    /* .ops = */ &(Ops),\
    /* .driver = */ NULL,\
    /* .log_flags = */ 3, /* DDK_LOG_ERROR | DDK_LOG_INFO */\
};\
extern const struct magenta_driver_note __magenta_driver_note__ __EXPORT;\
__SECTION(".note.magenta.driver." #Driver) MAGENTA_DRIVER_NOTE_ASAN \
const struct magenta_driver_note {\
    magenta_driver_note_t note;\
    mx_bind_inst_t binding[BindCount];\
} __magenta_driver_note__ = {\
    /* .note = */{\
        MAGENTA_DRIVER_NOTE_HEADER_INIT(__magenta_driver_note__),\
        MAGENTA_DRIVER_NOTE_PAYLOAD_INIT(Driver,VendorName,Version,BindCount),\
    },\
    /* .binding = */ {

#define MAGENTA_DRIVER_END(Driver) }};

//TODO: if we moved the Ops from the BEGIN() to END() macro we
//      could add a magenta_driver_note_t* to the mx_driver_rec_t,
//      define it in END(), and have only one symbol to dlsym()
//      when loading drivers

__END_CDECLS;
