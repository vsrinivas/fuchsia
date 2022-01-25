// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mcc-update.h"

#include <string.h>
#include <zircon/assert.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace wlan::testing {

zx_status_t HandleMccUpdate(struct iwl_host_cmd* cmd, SimMvmResponse* resp) {
  auto mcc_cmd = reinterpret_cast<const struct iwl_mcc_update_cmd*>(cmd->data[0]);

  resp->resize(sizeof(struct iwl_mcc_update_resp_v3));
  auto mcc_resp = reinterpret_cast<struct iwl_mcc_update_resp_v3*>(resp->data());
  memset(mcc_resp, 0, sizeof(*mcc_resp));
  mcc_resp->status = le32_to_cpu(MCC_RESP_NEW_CHAN_PROFILE);
  mcc_resp->mcc = mcc_cmd->mcc;
  mcc_resp->source_id = mcc_cmd->source_id;

  return ZX_OK;
}

}  // namespace wlan::testing
