// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include "tpm-commands.h"

uint32_t tpm_init_getrandom(struct tpm_getrandom_cmd *cmd, uint32_t bytes_requested) {
    cmd->hdr.tag = htobe16(TPM_TAG_RQU_COMMAND);
    cmd->hdr.total_len = htobe32(sizeof(*cmd));
    cmd->hdr.cmd_code = htobe32(TPM_ORD_GETRANDOM);
    cmd->bytes_requested = htobe32(bytes_requested);

    return sizeof(struct tpm_getrandom_resp) + bytes_requested;
}

uint32_t tpm_init_savestate(struct tpm_savestate_cmd *cmd) {
    cmd->hdr.tag = htobe16(TPM_TAG_RQU_COMMAND);
    cmd->hdr.total_len = htobe32(sizeof(*cmd));
    cmd->hdr.cmd_code = htobe32(TPM_ORD_SAVESTATE);
    return sizeof(struct tpm_savestate_resp);
}
