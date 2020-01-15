// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mvm.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/inspect.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/datapath.h"

namespace wlan {
namespace testing {

zx_status_t SimMvm::SendCmd(struct iwl_host_cmd* cmd, bool* notify_wait) {
  IWL_INSPECT_HOST_CMD(cmd);
  uint8_t opcode = iwl_cmd_opcode(cmd->id);
  uint8_t group_id = iwl_cmd_groupid(cmd->id);
  SimMvmResponse resp;  // Used by command functions to return packet.
  zx_status_t ret;

  *notify_wait = false;
  switch (group_id) {
    case LONG_GROUP:
      switch (opcode) {  // enum iwl_legacy_cmds
        // No state change for the following commands.

        // On real hardware, this command would reply a pakcet to unblock the driver waiting.
        // In the simulated code, we don't generate the packet. Instead, we unblock it directly.
        case PHY_CONFIGURATION_CMD:
          *notify_wait = true;
          // passthru

        case SHARED_MEM_CFG:
        case TX_ANT_CONFIGURATION_CMD:
        case PHY_DB_CMD:
        case PHY_CONTEXT_CMD:
        case REPLY_THERMAL_MNG_BACKOFF:
        case POWER_TABLE_CMD:
        case BT_CONFIG:
        case MAC_CONTEXT_CMD:
        case SCAN_OFFLOAD_REQUEST_CMD:
          return ZX_OK;

        case NVM_ACCESS_CMD:
          ret = nvm_.HandleCommand(cmd, &resp);
          break;

        default:
          printf("unsupported long command ID : %#x\n", cmd->id);
          IWL_INSPECT_HOST_CMD(cmd);
          return ZX_ERR_NOT_SUPPORTED;
      }
      break;

    case DATA_PATH_GROUP:
      switch (opcode) {  // enum iwl_data_path_subcmd_ids
        case DQA_ENABLE_CMD:
          ret = ZX_OK;
          break;

        default:
          printf("unsupported data path command ID : %#x\n", cmd->id);
          IWL_INSPECT_HOST_CMD(cmd);
          return ZX_ERR_NOT_SUPPORTED;
      }
      break;

    default:
      printf("unsupported command ID : %#x\n", cmd->id);
      IWL_INSPECT_HOST_CMD(cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }

  // Prepare the response packet buffer if the command requires a response.
  struct iwl_rx_packet* resp_pkt = reinterpret_cast<struct iwl_rx_packet*>(resp_buf_);
  if (cmd->flags & CMD_WANT_SKB) {
    resp_pkt->len_n_flags = cpu_to_le32(resp.size());
    resp_pkt->hdr.cmd = opcode;
    resp_pkt->hdr.group_id = group_id;
    resp_pkt->hdr.sequence = 0;
    ZX_ASSERT(resp.size() <= sizeof(resp_buf_));  // avoid overflow
    memcpy(resp_pkt->data, resp.data(), resp.size());
    cmd->resp_pkt = resp_pkt;
  } else {
    cmd->resp_pkt = nullptr;
  }

  return ret;
}

}  // namespace testing
}  // namespace wlan
