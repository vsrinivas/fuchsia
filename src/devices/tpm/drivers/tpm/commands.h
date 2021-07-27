// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TPM_DRIVERS_TPM_COMMANDS_H_
#define SRC_DEVICES_TPM_DRIVERS_TPM_COMMANDS_H_

#include <endian.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// The definitions here are spread across two parts of the TPM2 spec:
// - part 2, "structures".
// - part 3, "commands".
//
// https://trustedcomputinggroup.org/wp-content/uploads/TCG_TPM2_r1p59_Part2_Structures_pub.pdf
// https://trustedcomputinggroup.org/wp-content/uploads/TCG_TPM2_r1p59_Part3_Commands_pub.pdf

#define TPM_ST_NO_SESSIONS (0x8001)
#define TPM_CC_SHUTDOWN (0x0145)

#define TPM_SU_CLEAR 0x00
#define TPM_SU_STATE 0x01

struct TpmCmdHeader {
  uint16_t tag;
  uint32_t command_size;
  uint32_t command_code;
} __PACKED;

struct TpmShutdownCmd {
  TpmCmdHeader hdr;
  uint16_t shutdown_type;

  explicit TpmShutdownCmd(uint16_t type) : shutdown_type(htobe16(type)) {
    hdr = {.tag = htobe16(TPM_ST_NO_SESSIONS),
           .command_size = htobe32(sizeof(TpmShutdownCmd)),
           .command_code = htobe32(TPM_CC_SHUTDOWN)};
  }
} __PACKED;

struct TpmShutdownResponse {
  uint16_t tag;
  uint32_t response_size;
  uint32_t response_code;
};

#endif  // SRC_DEVICES_TPM_DRIVERS_TPM_COMMANDS_H_
