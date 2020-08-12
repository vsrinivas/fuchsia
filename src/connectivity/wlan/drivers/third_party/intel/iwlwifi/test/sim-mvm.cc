// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mvm.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/inspect.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/datapath.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/sta.h"

namespace wlan {
namespace testing {

static void build_response_with_status(SimMvmResponse* resp, uint32_t status) {
  // Check 'struct iwl_rx_packet'. The 'len_n_flags' includes the 'iwl_cmd_header' size and
  // its payload, which is 'iwl_cmd_response'.
  resp->resize(sizeof(struct iwl_cmd_header) + sizeof(struct iwl_cmd_response));

  struct iwl_cmd_response* cmd_resp = reinterpret_cast<struct iwl_cmd_response*>(resp->data());
  cmd_resp->status = cpu_to_le32(status);
}

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

        // On real hardware, this command will reply a pakcet to unblock the driver waiting.
        // In the simulated code, we don't generate the packet. Instead, we unblock it directly.
        case PHY_CONFIGURATION_CMD:
          // passthru

        // The driver code expects 2 notifications from the firmware in this command:
        //
        //   1. In iwl_mvm_time_event_send_add(), it waits for the notification of the completion of
        //      TIME_EVENT_CMD command.
        //   2. In iwl_mvm_protect_session(), it waits for the TIME_EVENT_NOTIFICATION.
        //
        // However, the current sim-mvm code can only unblock the first wait. So added
        // TODO(fxbug.dev/51671) to track this.
        case TIME_EVENT_CMD:
          // passthru

          // The above commands require unblock the notification.
          *notify_wait = true;

        case SHARED_MEM_CFG:
        case TX_ANT_CONFIGURATION_CMD:
        case PHY_DB_CMD:
        case PHY_CONTEXT_CMD:
        case REPLY_THERMAL_MNG_BACKOFF:
        case POWER_TABLE_CMD:
        case BT_CONFIG:
        case MAC_CONTEXT_CMD:
        case SCAN_OFFLOAD_REQUEST_CMD:
        case MAC_PM_POWER_TABLE:
        case SCD_QUEUE_CFG:
          return ZX_OK;

        // Command would return 'status' back to driver.
        case BINDING_CONTEXT_CMD:
          build_response_with_status(&resp, 0);
          ret = ZX_OK;
          break;

        case ADD_STA:
          build_response_with_status(&resp, ADD_STA_SUCCESS);
          ret = ZX_OK;
          break;

        case NVM_ACCESS_CMD:
          ret = nvm_.HandleCommand(cmd, &resp);
          break;

        default:
          IWL_ERR(nullptr, "unsupported long command ID : %#x\n", cmd->id);
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
          IWL_ERR(nullptr, "unsupported data path command ID : %#x\n", cmd->id);
          IWL_INSPECT_HOST_CMD(cmd);
          return ZX_ERR_NOT_SUPPORTED;
      }
      break;

    default:
      IWL_ERR(nullptr, "unsupported command ID : %#x\n", cmd->id);
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
