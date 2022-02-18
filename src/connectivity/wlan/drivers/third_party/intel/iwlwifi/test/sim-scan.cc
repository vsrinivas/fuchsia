// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/sim-scan.h"

#include <string.h>
#include <zircon/assert.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
}

namespace wlan::testing {

zx_status_t HandleScanAbort(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  // Since callbacks have not been implemented, pretend to notify scan cancellation.
  struct iwl_umac_scan_complete scan_notif {
    .status = IWL_SCAN_OFFLOAD_ABORTED
  };
  struct iwl_iobuf* io_buf = nullptr;
  iwl_mvm* mvm = iwl_trans_get_mvm(trans);
  if (iwl_iobuf_allocate_contiguous(trans->dev, sizeof(scan_notif) + sizeof(struct iwl_rx_packet),
                                    &io_buf) == ZX_OK) {
    struct iwl_rx_packet* pkt = reinterpret_cast<struct iwl_rx_packet*>(iwl_iobuf_virtual(io_buf));
    // Most fields are not cared but initialized with known values.
    pkt->len_n_flags = cpu_to_le32(0);
    pkt->hdr.cmd = 0;
    pkt->hdr.group_id = 0;
    pkt->hdr.sequence = 0;
    memcpy(pkt->data, &scan_notif, sizeof(scan_notif));
    struct iwl_rx_cmd_buffer* rx_cmd = reinterpret_cast<struct iwl_rx_cmd_buffer*>(io_buf);
    rx_cmd->_iobuf = io_buf;
    rx_cmd->_offset = 0;
    iwl_mvm_rx_umac_scan_complete_notif(mvm, rx_cmd);
    iwl_iobuf_release(io_buf);
    return ZX_OK;
  }
  return ZX_ERR_NO_RESOURCES;
}

}  // namespace wlan::testing
