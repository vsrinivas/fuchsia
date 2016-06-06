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
#include <endian.h>
#include "tpm_commands.h"

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
