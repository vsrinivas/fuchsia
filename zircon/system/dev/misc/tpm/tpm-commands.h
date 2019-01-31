// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>

#define TPM_TAG_RSP_COMMAND 196

#define TPM_ST_NO_SESSIONS 0x8001
#define TPM_CC_SHUTDOWN    0x00000145
#define TPM_CC_GET_RANDOM  0x0000017B

// All TPM fields are big-endian.
// The init functions that return uint32_t return the number of bytes needed
// for the response structure.

#define TPM_FIXED_LEN_CMD_INIT(cmd, cmd_code) { \
    .tag = TPM_TAG_RQU_COMMAND, \
    .total_len = sizeof(cmd), \
    .cmd_code = cmd_code }

struct tpm_cmd_header {
    uint16_t tag;
    uint32_t total_len;
    uint32_t cmd_code;
} __PACKED;

struct tpm_resp_header {
    uint16_t tag;
    uint32_t total_len;
    uint32_t return_code;
} __PACKED;

struct tpm_getrandom_cmd {
    struct tpm_cmd_header hdr;
    uint16_t bytes_requested;
} __PACKED;
struct tpm_getrandom_resp {
    struct tpm_resp_header hdr;
    uint16_t bytes_returned;
    uint8_t bytes[];
} __PACKED;
uint32_t tpm_init_getrandom(struct tpm_getrandom_cmd *cmd, uint16_t bytes_requested);

// Shutdown types
#define TPM_SU_CLEAR 0
#define TPM_SU_STATE 1

struct tpm_shutdown_cmd {
    struct tpm_cmd_header hdr;
    uint16_t shutdown_type;
} __PACKED;
struct tpm_shutdown_resp {
    struct tpm_resp_header hdr;
} __PACKED;
uint32_t tpm_init_shutdown(struct tpm_shutdown_cmd *cmd, uint16_t type);
