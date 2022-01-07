// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm.h"

#include <string.h>
#include <zircon/assert.h>

#include <algorithm>
#include <vector>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/nvm-reg.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-nvm-data.inc"

namespace wlan::testing {

std::vector<uint8_t> SimNvm::HandleChunkRead(uint8_t target, uint16_t type, uint16_t offset,
                                             uint16_t length) {
  auto sections = GetDefaultNvmSections();
  for (auto iter : sections) {
    if (iter.target != target || iter.type != type) {
      continue;
    }

    // Offsetting a null pointer is undefined behavior, even if the offset is zero. Passing a null
    // pointer to memcpy (or any similar function) is also undefined behavior.
    if (iter.data.data() == nullptr) {
      // There is no other <target, type> pair existing so that we can have early return. See the
      // comment of GetDefaultNvmSections().
      return {};
    }

    // Handle the boundary cases.
    const size_t read_offset = std::min<size_t>(offset, iter.data.size());
    const size_t read_length = std::min<size_t>(length, iter.data.size() - read_offset);
    std::vector<uint8_t> ret;
    ret.reserve(read_length);
    std::copy_n(iter.data.begin() + read_offset, read_length, std::back_inserter(ret));
    return ret;
  }

  return {};  // No segment found.
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
      std::vector<uint8_t> payload = HandleChunkRead(target, type, offset, length);
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
