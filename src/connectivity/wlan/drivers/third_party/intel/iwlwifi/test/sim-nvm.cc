// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"

#include <stdint.h>

#include <vector>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/nvm-reg.h"
extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace wlan::testing {

ByteArray SimNvm::HandleChunkRead(uint8_t target, uint16_t type, uint16_t offset, uint16_t length) {
  for (auto iter = default_sections.begin(); iter != default_sections.end(); iter++) {
    if (iter->target != target || iter->type != type) {
      continue;
    }

    // Handle the boundry cases.
    size_t size = iter->data.size();
    if (offset > size) {
      offset = size;
    }
    if (offset + length > size) {
      length = size - offset;
    }

    ByteArray ret(length);
    memcpy(ret.data(), &iter->data[offset], ret.size());
    return ret;
  }

  return ByteArray(0);  // No segment found.
}

zx_status_t SimNvm::HandleCommand(struct iwl_host_cmd* cmd, SimMvmResponse* resp) {
  // Currently we only support the first data segment.
  ZX_ASSERT(!cmd->data[1]);

  const struct iwl_nvm_access_cmd* nvm_access_cmd =
      reinterpret_cast<const struct iwl_nvm_access_cmd*>(cmd->data[0]);
  uint8_t target = nvm_access_cmd->target;
  uint16_t type = le16_to_cpu(nvm_access_cmd->type);
  uint16_t offset = le16_to_cpu(nvm_access_cmd->offset);
  uint16_t length = le16_to_cpu(nvm_access_cmd->length);

  switch (nvm_access_cmd->op_code) {
    case NVM_READ_OPCODE: {
      ByteArray payload = HandleChunkRead(target, type, offset, length);
      resp->resize(sizeof(struct iwl_nvm_access_resp) + payload.size());
      struct iwl_nvm_access_resp* nvm_resp =
          reinterpret_cast<struct iwl_nvm_access_resp*>(resp->data());
      nvm_resp->offset = cpu_to_le16(offset);
      nvm_resp->type = cpu_to_le16(type);
      nvm_resp->status = cpu_to_le16(READ_NVM_CHUNK_SUCCEED);
      nvm_resp->length = cpu_to_le16(payload.size());
      memcpy(nvm_resp->data, payload.data(), payload.size());
      return ZX_OK;
    }

    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace wlan::testing
