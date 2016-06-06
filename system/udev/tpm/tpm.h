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

#include <magenta/types.h>
#include <stddef.h>
#include <stdint.h>

extern void *tpm_base;
extern mx_handle_t irq_handle;

enum locality {
    LOCALITY0,
    LOCALITY1,
    LOCALITY2,
    LOCALITY3,
    LOCALITY4,
};

enum irq_type {
    IRQ_DATA_AVAIL = 0x01,
    IRQ_LOCALITY_CHANGE = 0x04,
};

enum tpm_result {
    TPM_SUCCESS = 0x0,
    TPM_BAD_PARAMETER = 0x3,
    TPM_DEACTIVATED = 0x6,
    TPM_DISABLED = 0x7,
    TPM_DISABLED_CMD = 0x8,
    TPM_FAIL = 0x9,
    TPM_BAD_ORDINAL = 0xa,
    TPM_RETRY = 0x800,
};

mx_status_t tpm_set_irq(enum locality loc, uint8_t vector);
mx_status_t tpm_enable_irq_type(enum locality loc, enum irq_type type);
mx_status_t tpm_disable_irq_type(enum locality loc, enum irq_type type);

// Request use of the given locality
mx_status_t tpm_request_use(enum locality loc);
// Wait for actual access to the locality
mx_status_t tpm_wait_for_locality(enum locality loc);

mx_status_t tpm_send_cmd(enum locality loc, uint8_t* cmd, size_t len);
// Returns the total number of bytes in the response, may be less than max_len.
// If negative, the return value is an error code
ssize_t tpm_recv_resp(enum locality loc, uint8_t* resp, size_t max_len);
