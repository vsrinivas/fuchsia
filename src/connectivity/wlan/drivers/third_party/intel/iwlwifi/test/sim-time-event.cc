// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-time-event.h"

#include <string.h>
#include <zircon/assert.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace wlan::testing {

zx_status_t HandleTimeEvent(struct iwl_host_cmd* cmd, SimMvmResponse* resp) {
  auto tm_cmd = reinterpret_cast<const struct iwl_time_event_cmd*>(cmd->data[0]);

  resp->resize(sizeof(struct iwl_time_event_resp));
  auto tm_resp = reinterpret_cast<struct iwl_time_event_resp*>(resp->data());
  memset(tm_resp, 0, sizeof(*tm_resp));
  // Copy the 'id' field from the command and fill a fake value in 'unique_id' field.
  tm_resp->id = le32_to_cpu(tm_cmd->id);
  tm_resp->unique_id = le32_to_cpu(kFakeUniqueId);

  return ZX_OK;
}

}  // namespace wlan::testing
