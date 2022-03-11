// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-mcc-update.h"

#include <string.h>
#include <zircon/assert.h>

#include <iterator>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"

namespace wlan::testing {

zx_status_t HandleMccUpdate(struct iwl_host_cmd* cmd, SimMvmResponse* resp) {
  auto mcc_cmd = reinterpret_cast<const struct iwl_mcc_update_cmd*>(cmd->data[0]);

  __le32 channels[] = {
      // actually the channel flags.
      0x034b,  // Ch1: VALID IBSS ACTIVE GO_CONCURRENT 20MHZ 40MHZ
      0x0f4a,  // Ch2:        BSS ACTIVE GO_CONCURRENT 20MHZ 40MHZ 80MHZ 160MHZ
      0x0f43,  // Ch3: VALID  BSS        GO_CONCURRENT 20MHZ 40MHZ 80MHZ 160MHZ
      0x0f4b,  // Ch4: VALID IBSS ACTIVE GO_CONCURRENT 20MHZ 40MHZ 80MHZ 160MHZ
  };
  static constexpr size_t n_chan = std::size(channels);
  size_t resp_size =
      sizeof(struct iwl_mcc_update_resp_v3) + n_chan * sizeof(__le32);  // mcc_resp->channels[0]
  resp->resize(resp_size);
  auto mcc_resp = reinterpret_cast<struct iwl_mcc_update_resp_v3*>(resp->data());
  memset(mcc_resp, 0, resp_size);
  mcc_resp->status = le32_to_cpu(MCC_RESP_NEW_CHAN_PROFILE);
  mcc_resp->mcc = mcc_cmd->mcc;
  mcc_resp->source_id = mcc_cmd->source_id;

  // The channel list that this country supports. Currently we always return the same one.
  // Note that this doesn't match the list in iwl_ext_nvm_channels / iwl_nvm_channels.
  // We just return it for easy unit testing.
  mcc_resp->n_channels = n_chan;  // See enum iwl_nvm_channel_flags below:
  memcpy(mcc_resp->channels, channels, sizeof(channels));
  return ZX_OK;
}

}  // namespace wlan::testing
