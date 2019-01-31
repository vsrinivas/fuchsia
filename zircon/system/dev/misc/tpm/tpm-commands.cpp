// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include "tpm-commands.h"

uint32_t tpm_init_getrandom(struct tpm_getrandom_cmd *cmd, uint16_t bytes_requested) {
    cmd->hdr.tag = htobe16(TPM_ST_NO_SESSIONS);
    cmd->hdr.total_len = htobe32(sizeof(*cmd));
    cmd->hdr.cmd_code = htobe32(TPM_CC_GET_RANDOM);
    cmd->bytes_requested = htobe16(bytes_requested);

    return static_cast<uint32_t>(sizeof(struct tpm_getrandom_resp)) + bytes_requested;
}

uint32_t tpm_init_shutdown(struct tpm_shutdown_cmd *cmd, uint16_t type) {
    cmd->hdr.tag = htobe16(TPM_ST_NO_SESSIONS);
    cmd->hdr.total_len = htobe32(sizeof(*cmd));
    cmd->hdr.cmd_code = htobe32(TPM_CC_SHUTDOWN);
    cmd->shutdown_type = htobe16(type);
    return static_cast<uint32_t>(sizeof(struct tpm_shutdown_resp));
}
