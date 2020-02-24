// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_SPI_PACKETS_H_
#define SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_SPI_PACKETS_H_

#include <stdint.h>

namespace ot {

constexpr uint32_t kSpiPacketCrc32InitValue = 0xffffffff;
constexpr uint8_t kNlBootloaderVersionMaxLength = 16;

// Top level commands
enum {
  BL_SPI_CMD_INVALID = 0x00,      // just in case someone passes 0
  BL_SPI_CMD_GET_VERSION = 0x01,  // returns bootloader version
  BL_SPI_CMD_UNUSED = 0x02,
  BL_SPI_CMD_FLASH_ERASE = 0x03,  // erases app FLASH region given address and length
  BL_SPI_CMD_FLASH_WRITE = 0x04,  // writes a range of app FLASH region
  BL_SPI_CMD_FLASH_VERIFY =
      0x05,                  // computes CRC of FLASH region and compares against given CRC32 value
  BL_SPI_CMD_UNLOCK = 0x06,  // given an unlock token, if valid, unlock debugger access
};

// Error status values in response packet
enum {
  BL_ERROR_NONE = 0,
  BL_ERROR_INVALID_ARGS = -1,
  BL_ERROR_VERIFY_FAILED = -2,
};

/* Command packets sent by host to bootloader. The simplest packet is
 * one that consists of just a cmd encoded completely in the header packet.
 */
typedef struct {
  uint32_t crc32;
  uint16_t length;  // length of packet, including crc32 and length fields
  uint8_t cmd;
} __attribute__((packed)) NlSpiBlBasicCmd;

/* Flash erase cmd is the BasicCmd plus some additional arguments */
typedef struct {
  NlSpiBlBasicCmd cmd;
  uint32_t address;
  uint32_t length;
} __attribute__((packed)) NlSpiBlCmdFlashErase;

/* Flash write cmd is the basic_cmd plus some additional arguments */

// spidev has a default internal buffer size of 4096, so we could
// set kSpiPacketMaxPayloadSize to 4096 minus our basic_cmd size,
// but since we allocate this max packet statically in order to
// be able to receive the largest possible packet, it's rather
// a waste of RAM if we never actually send any packet that large.
// instead, set it to the max packet payload, which is the size
// of the flash_write packet minus the basic_cmd
constexpr uint32_t kSpiPacketMaxFlashWriteDataSize = 2048;

typedef struct {
  NlSpiBlBasicCmd cmd;
  uint32_t address;
  uint8_t data[kSpiPacketMaxFlashWriteDataSize];  // the actual bytes is the cmd.length -
                                                  // sizeof(cmd) - sizeof(address)
} __attribute__((packed)) NlSpiBlCmdFlashWrite;

constexpr size_t kSpiPacketMaxSize = sizeof(NlSpiBlCmdFlashWrite);
constexpr size_t kSpiPacketMaxPayloadSize = kSpiPacketMaxSize - sizeof(NlSpiBlBasicCmd);

/* Generic packet representing largest packet we could receive in the bootloader */
typedef struct {
  NlSpiBlBasicCmd cmd;
  uint8_t payload[kSpiPacketMaxPayloadSize];
} __attribute__((packed)) NlSpiCmdGeneric;

/* Flash verify cmd does a crc32 check and returns result */
typedef struct {
  NlSpiBlBasicCmd cmd;
  uint32_t address;
  uint32_t length;
  uint32_t crc32_check;
} __attribute__((packed)) NlSpiCmdFlashVerify;

/* Debugger unlock cmd needs to have an authenticated unlock_token */
typedef struct {
  NlSpiBlBasicCmd cmd;
  uint8_t unlock_token[64];  // supports up to secp256r1, though we currently only use secp224r1
                             // based tokens
} __attribute__((packed)) NlSpiBlCmdUnlock;

/* Response packets sent by bootloader */
typedef struct {
  NlSpiBlBasicCmd cmd;
  int32_t status;
} __attribute__((packed)) NlSpiBlBasicResponse;

/* Response for the get_version cmd */
typedef struct {
  NlSpiBlBasicResponse response;
  uint8_t version[kNlBootloaderVersionMaxLength];
} __attribute__((packed)) NlSpiBlGetVersionResponse;

}  // namespace ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_DRIVERS_OT_RADIO_OT_RADIO_BOOTLOADER_SPI_PACKETS_H_
