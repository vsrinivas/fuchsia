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

zx_status_t SimMvm::SendCmd(struct iwl_host_cmd* cmd) {
  IWL_INSPECT_HOST_CMD(cmd);
  uint8_t opcode = iwl_cmd_opcode(cmd->id);

  switch (iwl_cmd_groupid(cmd->id)) {
    case LONG_GROUP:
      switch (opcode) {  // enum iwl_legacy_cmds
        // No state change for the following commands.
        case SHARED_MEM_CFG:
        case TX_ANT_CONFIGURATION_CMD:
        case PHY_DB_CMD:
        case PHY_CONFIGURATION_CMD:
        case PHY_CONTEXT_CMD:
        case REPLY_THERMAL_MNG_BACKOFF:
        case POWER_TABLE_CMD:
          return ZX_OK;

        default:
          IWL_ERR(cmd, "unsupported long command ID : %#x\n", cmd->id);
          IWL_INSPECT_HOST_CMD(cmd);
          return ZX_ERR_NOT_SUPPORTED;
      }

    case DATA_PATH_GROUP:
      switch (opcode) {  // enum iwl_data_path_subcmd_ids
        case DQA_ENABLE_CMD:
          return ZX_OK;

        default:
          IWL_ERR(cmd, "unsupported data path command ID : %#x\n", cmd->id);
          IWL_INSPECT_HOST_CMD(cmd);
          return ZX_ERR_NOT_SUPPORTED;
      }

    default:
      IWL_ERR(cmd, "unsupported command ID : %#x\n", cmd->id);
      IWL_INSPECT_HOST_CMD(cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace testing
}  // namespace wlan
