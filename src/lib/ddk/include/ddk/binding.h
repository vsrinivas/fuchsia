// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_BINDING_H_
#define SRC_LIB_DDK_INCLUDE_DDK_BINDING_H_

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// COAABBBB VVVVVVVV  Condition Opcode paramA paramB Value

#define OP_ABORT 0x0  // if (cond) return no-match
#define OP_MATCH 0x1  // if (cond) return match
#define OP_GOTO 0x2  // if (cond) advance to next LABEL(paramA)
#define OP_LABEL 0x5  // no-op, labels line with paramA

#define COND_AL 0x0  // true
#define COND_EQ 0x1  // bind(paramB) == Value
#define COND_NE 0x2  // bind(paramB) != Value
#define COND_GT 0x3  // bind(paramB) > Value
#define COND_LT 0x4  // bind(paramB) < Value
#define COND_GE 0x5  // bind(paramB) >= Value
#define COND_LE 0x6  // bind(paramB) <= Value

// branches are forward-only
// branches always go to the first matching LABEL
// branches that cannot find a matching LABEL are treated as ABORTs
// there is an implied unconditional ABORT after the last instruction
// flags are initially zero, may be set/cleared with SET/CLEAR
// flags may be tested by comparison against BIND_FLAGS

#define BINDINST(c, o, a, b, v) \
  { (((c)&0xF) << 28) | (((o)&0xF) << 24) | (((a)&0xFF) << 16) | ((b)&0xFFFF), (v), 0 /* debug */ }

#define BINDINST_CC(n) ((n) >> 28)
#define BINDINST_OP(n) (((n) >> 24) & 0xF)
#define BINDINST_PA(n) (((n) >> 16) & 0xFF)
#define BINDINST_PB(n) ((n)&0xFFFF)

#define BI_ABORT() BINDINST(COND_AL, OP_ABORT, 0, 0, 0)
#define BI_MATCH() BINDINST(COND_AL, OP_MATCH, 0, 0, 0)
#define BI_GOTO(n) BINDINST(COND_AL, OP_GOTO, n, 0, 0)
#define BI_LABEL(n) BINDINST(COND_AL, OP_LABEL, n, 0, 0)

#define BI_ABORT_IF(c, b, v) BINDINST(COND_##c, OP_ABORT, 0, b, v)
#define BI_MATCH_IF(c, b, v) BINDINST(COND_##c, OP_MATCH, 0, b, v)
#define BI_GOTO_IF(c, b, v, n) BINDINST(COND_##c, OP_GOTO, n, b, v)

// for drivers that only want to be bound on user request
#define BI_ABORT_IF_AUTOBIND BI_ABORT_IF(NE, BIND_AUTOBIND, 0)

// global binding variables at 0x00XX
#define BIND_FLAGS 0x0000  // value of the flags register
#define BIND_PROTOCOL 0x0001  // primary protocol of the device
#define BIND_AUTOBIND 0x0002  // if this is an automated bind/load

// pci binding variables at 0x01XX
#define BIND_PCI_VID 0x0100
#define BIND_PCI_DID 0x0101
#define BIND_PCI_CLASS 0x0102
#define BIND_PCI_SUBCLASS 0x0103
#define BIND_PCI_INTERFACE 0x0104
#define BIND_PCI_REVISION 0x0105

// usb binding variables at 0x02XX
// these are used for both ZX_PROTOCOL_USB and ZX_PROTOCOL_USB_FUNCTION
#define BIND_USB_VID 0x0200
#define BIND_USB_PID 0x0201
#define BIND_USB_CLASS 0x0202
#define BIND_USB_SUBCLASS 0x0203
#define BIND_USB_PROTOCOL 0x0204

// Platform bus binding variables at 0x03XX
#define BIND_PLATFORM_DEV_VID 0x0300
#define BIND_PLATFORM_DEV_PID 0x0301
#define BIND_PLATFORM_DEV_DID 0x0302
#define BIND_PLATFORM_PROTO 0x0303

// ACPI binding variables at 0x04XX
// The _HID is a 7- or 8-byte string. Because a bind property is 32-bit, use 2
// properties to bind using the _HID. They are encoded in big endian order for
// human readability. In the case of 7-byte _HID's, the 8th-byte shall be 0.
#define BIND_ACPI_HID_0_3 0x0400  // char 0-3
#define BIND_ACPI_HID_4_7 0x0401  // char 4-7
// The _CID may be a valid HID value or a bus-specific string. The ACPI bus
// driver only publishes those that are valid HID values.
#define BIND_ACPI_CID_0_3 0x0402  // char 0-3
#define BIND_ACPI_CID_4_7 0x0403  // char 4-7

// Intel HDA Codec binding variables at 0x05XX
#define BIND_IHDA_CODEC_VID 0x0500
#define BIND_IHDA_CODEC_DID 0x0501
#define BIND_IHDA_CODEC_MAJOR_REV 0x0502
#define BIND_IHDA_CODEC_MINOR_REV 0x0503
#define BIND_IHDA_CODEC_VENDOR_REV 0x0504
#define BIND_IHDA_CODEC_VENDOR_STEP 0x0505

// Serial binding variables at 0x06XX
#define BIND_SERIAL_CLASS 0x0600
#define BIND_SERIAL_VID 0x0601
#define BIND_SERIAL_PID 0x0602

// NAND binding variables at 0x07XX
#define BIND_NAND_CLASS 0x0700

// Bluetooth binding variables at 0x08XX
#define BIND_BT_GATT_SVC_UUID16 0x0800
// 128-bit UUID is split across 4 32-bit unsigned ints
#define BIND_BT_GATT_SVC_UUID128_1 0x0801
#define BIND_BT_GATT_SVC_UUID128_2 0x0802
#define BIND_BT_GATT_SVC_UUID128_3 0x0803
#define BIND_BT_GATT_SVC_UUID128_4 0x0804

// SDIO binding variables at 0x09XX
#define BIND_SDIO_VID 0x0900
#define BIND_SDIO_PID 0x0901
#define BIND_SDIO_FUNCTION 0x0902

// I2C binding variables at 0x0A0X
#define BIND_I2C_CLASS 0x0A00
#define BIND_I2C_BUS_ID 0x0A01
#define BIND_I2C_ADDRESS 0x0A02

// GPIO binding variables at 0x0A1X
#define BIND_GPIO_PIN 0x0A10

// POWER binding variables at 0x0A2X
#define BIND_POWER_DOMAIN 0x0A20
#define BIND_POWER_DOMAIN_COMPOSITE 0x0A21

// POWER binding variables at 0x0A3X
#define BIND_CLOCK_ID 0x0A30

// SPI binding variables at 0x0A4X
#define BIND_SPI_CLASS 0x0A40
#define BIND_SPI_BUS_ID 0x0A41
#define BIND_SPI_CHIP_SELECT 0x0A42

// PWM binding variables at 0x0A5X
#define BIND_PWM_ID 0x0A50

// Init Step binding variables at 0x0A6X
#define BIND_INIT_STEP 0x0A60

// Codec binding variables at 0x0A7X
#define BIND_CODEC_INSTANCE 0x0A70

// Fuchsia-defined topological path properties are at 0x0B00 through 0x0B7F.
// Vendor-defined topological path properties are at 0x0B80 to 0x0BFF.
// For vendor properties, it is recommended that a vendor ID be included
// and checked via some other property.
#define BIND_TOPO_START 0x0B00
#define BIND_TOPO_PCI 0x0B00
#define BIND_TOPO_I2C 0x0B01
#define BIND_TOPO_SPI 0x0B02
#define BIND_TOPO_VENDOR_START 0x0B80
#define BIND_TOPO_VENDOR_END 0x0BFF
#define BIND_TOPO_END 0x0BFF

#define BIND_TOPO_PCI_PACK(bus, dev, func) (((bus) << 8) | (dev << 3) | (func))
#define BIND_TOPO_PCI_UNPACK_BUS(topo) (((topo) >> 8) & 0xff)
#define BIND_TOPO_PCI_UNPACK_DEVICE(topo) (((topo) >> 3) & 0x1f)
#define BIND_TOPO_PCI_UNPACK_FUNCTION(topo) ((topo)&0x7)

#define BIND_TOPO_I2C_PACK(addr) ((addr))
#define BIND_TOPO_I2C_UNPACK(topo) ((topo))

#define BIND_TOPO_SPI_PACK(bus, chip_select) (((bus) << 8) | (chip_select))
#define BIND_TOPO_SPI_UNPACK_BUS_ID(topo) (((topo) >> 8) && 0xff)
#define BIND_TOPO_SPI_UNPACK_CHIP_SELECT(topo) ((topo)&0xff)

typedef struct zx_bind_inst {
  uint32_t op;
  uint32_t arg;
  uint32_t debug;
} zx_bind_inst_t;

typedef struct zx_device_prop {
  uint16_t id;
  uint16_t reserved;
  uint32_t value;
} zx_device_prop_t;

// simple example
#if 0
zx_bind_inst_t i915_binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1616), // broadwell
    BI_MATCH_IF(EQ, BIND_PCI_DID, 0x1916), // skylake
    BI_ABORT(),
};
#endif

#define ZIRCON_NOTE_NAME "Zircon"
#define ZIRCON_NOTE_DRIVER 0x31565244  // DRV1

typedef struct {
  // Elf64_Nhdr fields:
  uint32_t namesz;
  uint32_t descsz;
  uint32_t type;
  // ELF note name.  namesz is the exact size of the name (including '\0'),
  // but the storage size is always rounded up to a multiple of 4 bytes.
  char name[(sizeof(ZIRCON_NOTE_NAME) + 3) & -4];
} zircon_driver_note_header_t;

#define ZIRCON_DRIVER_NOTE_HEADER_INIT(object)                                  \
  {                                                                             \
    /* .namesz = */ sizeof(ZIRCON_NOTE_NAME),                                   \
        /* .descsz = */ (sizeof(object) - sizeof(zircon_driver_note_header_t)), \
        /* .type = */ ZIRCON_NOTE_DRIVER, /* .name = */ ZIRCON_NOTE_NAME,       \
  }

typedef struct {
  // See flag bits below.
  uint32_t flags;

  // Driver Metadata
  uint32_t bindcount;
  uint32_t reserved0;
  char name[32];
  char vendor[16];
  char version[16];

  // Driver Bind Program follows
} zircon_driver_note_payload_t;

// Flag bits in the driver note:

// Driver is built with `-fsanitize=address` and can only be loaded into a
// devhost that supports the ASan runtime.
#define ZIRCON_DRIVER_NOTE_FLAG_ASAN (1u << 0)

#define ZIRCON_DRIVER_NOTE_PAYLOAD_INIT(Driver, VendorName, Version, BindCount)                    \
  {                                                                                                \
    /* .flags = */ ZIRCON_DRIVER_NOTE_FLAGS, /* .bindcount = */ (BindCount), /* .reserved0 = */ 0, \
        /* .name = */ #Driver, /* .vendor = */ VendorName, /* .version = */ Version,               \
  }

#define ZIRCON_DRIVER_NOTE_FLAGS \
  (__has_feature(address_sanitizer) ? ZIRCON_DRIVER_NOTE_FLAG_ASAN : 0)

typedef struct {
  zircon_driver_note_header_t header;
  zircon_driver_note_payload_t payload;
} zircon_driver_note_t;

static_assert(offsetof(zircon_driver_note_t, payload) == sizeof(zircon_driver_note_header_t),
              "alignment snafu?");

// Without this, ASan will add redzone padding after the object, which
// would make it invalid ELF note format.
#if __has_feature(address_sanitizer)
#define ZIRCON_DRIVER_NOTE_ASAN __attribute__((no_sanitize("address")))
#else
#define ZIRCON_DRIVER_NOTE_ASAN
#endif

// GCC has a quirk about how '__attribute__((visibility("default")))'
// (__EXPORT here) works for const variables in C++.  The attribute has no
// effect when used on the definition of a const variable, and GCC gives a
// warning/error about that.  The attribute must appear on the "extern"
// declaration of the variable instead.

// We explicitly align the note to 4 bytes.  That's its natural alignment
// anyway, but the compilers sometimes like to over-align as an
// optimization while other tools sometimes like to complain if SHT_NOTE
// sections are over-aligned (since this could result in padding being
// inserted that makes it violate the ELF note format).  Standard C11
// doesn't permit alignas(...) on a type but we could use __ALIGNED(4) on
// all the types (i.e. GNU __attribute__ syntax instead of C11 syntax).
// But the alignment of the types is not actually the issue: it's the
// compiler deciding to over-align the individual object regardless of its
// type's alignment, so we have to explicitly set the alignment of the
// object to defeat any compiler default over-alignment.

#define ZIRCON_DRIVER_BEGIN(Driver, Ops, VendorName, Version, BindCount)           \
  const zx_driver_ops_t* __zircon_driver_ops__ __EXPORT = &(Ops);                  \
  zx_driver_rec_t __zircon_driver_rec__ __EXPORT = {                               \
      /* .ops = */ &(Ops),                                                         \
      /* .driver = */ NULL,                                                        \
      /* .log_flags = */ 0,                                                        \
  };                                                                               \
  extern const struct zircon_driver_note __zircon_driver_note__ __EXPORT;          \
  alignas(4) __SECTION(".note.zircon.driver." #Driver)                             \
      ZIRCON_DRIVER_NOTE_ASAN const struct zircon_driver_note {                    \
    zircon_driver_note_t note;                                                     \
    zx_bind_inst_t binding[BindCount];                                             \
  } __zircon_driver_note__ = {                                                     \
      /* .note = */ {                                                              \
          ZIRCON_DRIVER_NOTE_HEADER_INIT(__zircon_driver_note__),                  \
          ZIRCON_DRIVER_NOTE_PAYLOAD_INIT(Driver, VendorName, Version, BindCount), \
      },                                                                           \
      /* .binding = */ {
#define ZIRCON_DRIVER_END(Driver) \
  }                               \
  }                               \
  ;

// TODO: if we moved the Ops from the BEGIN() to END() macro we
//      could add a zircon_driver_note_t* to the zx_driver_rec_t,
//      define it in END(), and have only one symbol to dlsym()
//      when loading drivers

__END_CDECLS

#endif  // SRC_LIB_DDK_INCLUDE_DDK_BINDING_H_
