// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERIAL_IO_H_
#define ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERIAL_IO_H_

#include <efi/types.h>

#define EFI_SERIAL_IO_PROTOCOL_GUID                                                \
  {                                                                                \
    0xBB25CF6F, 0xF1D4, 0x11D2, { 0x9a, 0x0c, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0xfd } \
  }
extern efi_guid SerialIoProtocol;

#define EFI_SERIAL_TERMINAL_DEVICE_TYPE_GUID                                       \
  {                                                                                \
    0x6ad9a60f, 0x5815, 0x4c7c, { 0x8a, 0x10, 0x50, 0x53, 0xd2, 0xbf, 0x7a, 0x1b } \
  }

#define EFI_SERIAL_IO_PROTOCOL_REVISION 0x00010000
#define EFI_SERIAL_IO_PROTOCOL_REVISION1p1 0x00010001

//
// Control bits.
//
#define EFI_SERIAL_CLEAR_TO_SEND 0x0010
#define EFI_SERIAL_DATA_SET_READY 0x0020
#define EFI_SERIAL_RING_INDICATE 0x0040
#define EFI_SERIAL_CARRIER_DETECT 0x0080
#define EFI_SERIAL_REQUEST_TO_SEND 0x0002
#define EFI_SERIAL_DATA_TERMINAL_READY 0x0001
#define EFI_SERIAL_INPUT_BUFFER_EMPTY 0x0100
#define EFI_SERIAL_OUTPUT_BUFFER_EMPTY 0x0200
#define EFI_SERIAL_HARDWARE_LOOPBACK_ENABLE 0x1000
#define EFI_SERIAL_SOFTWARE_LOOPBACK_ENABLE 0x2000
#define EFI_SERIAL_HARDWARE_FLOW_CONTROL_ENABLE 0x4000

typedef enum efi_parity_type {
  DefaultParity,
  NoParity,
  EvenParity,
  OddParity,
  MarkParity,
  SpaceParity
} efi_parity_type;

typedef enum efi_stop_bits_type {
  DefaultStopBits,
  OneStopBit,       // 1 stop bit
  OneFiveStopBits,  // 1.5 stop bits
  TwoStopBits       // 2 stop bits
} efi_stop_bits_type;

typedef struct serial_io_mode {
  uint32_t ControlMask;

  // current Attributes
  uint32_t Timeout;
  uint64_t BaudRate;
  uint32_t ReceiveFifoDepth;
  uint32_t DataBits;
  efi_parity_type Parity;
  efi_stop_bits_type StopBits;
} serial_io_mode;

typedef struct efi_serial_io_protocol {
  uint32_t Revision;

  efi_status (*Reset)(struct efi_serial_io_protocol* self) EFIAPI;
  efi_status (*SetAttributes)(struct efi_serial_io_protocol* self, uint64_t BaudRate,
                              uint32_t ReceiveFifoDepth, uint32_t Timeout, efi_parity_type Parity,
                              uint8_t DataBits, efi_stop_bits_type StopBits) EFIAPI;
  efi_status (*SetControl)(struct efi_serial_io_protocol* self, uint32_t Control) EFIAPI;
  efi_status (*GetControl)(struct efi_serial_io_protocol* self, uint32_t* Control) EFIAPI;
  efi_status (*Write)(struct efi_serial_io_protocol* self, uint64_t* BufferSize,
                      void* Buffer) EFIAPI;
  efi_status (*Read)(struct efi_serial_io_protocol* self, uint64_t* BufferSize,
                     void* Buffer) EFIAPI;

  serial_io_mode* Mode;
  const struct elf_guid* DeviceTypeGuid;  // Revision 1.1
} efi_serial_io_protocol;

#endif  // ZIRCON_KERNEL_LIB_EFI_INCLUDE_EFI_PROTOCOL_SERIAL_IO_H_
