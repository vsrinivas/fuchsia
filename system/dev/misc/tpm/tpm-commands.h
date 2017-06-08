// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>

#define TPM_TAG_RQU_COMMAND 193
#define TPM_TAG_RSP_COMMAND 196

#define TPM_ORD_GETRANDOM 70
#define TPM_ORD_SAVESTATE 152

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
    uint32_t bytes_requested;
} __PACKED;
struct tpm_getrandom_resp {
    struct tpm_resp_header hdr;
    uint32_t bytes_returned;
    uint8_t bytes[];
} __PACKED;
uint32_t tpm_init_getrandom(struct tpm_getrandom_cmd *cmd, uint32_t bytes_requested);

struct tpm_savestate_cmd {
    struct tpm_cmd_header hdr;
} __PACKED;
struct tpm_savestate_resp {
    struct tpm_resp_header hdr;
} __PACKED;
uint32_t tpm_init_savestate(struct tpm_savestate_cmd *cmd);
