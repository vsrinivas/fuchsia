//
// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#pragma once

#include <system/compiler.h>

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
