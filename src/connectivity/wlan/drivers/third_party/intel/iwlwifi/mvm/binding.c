/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <string.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/fw-api.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

struct iwl_mvm_iface_iterator_data {
  struct iwl_mvm_vif* ignore_vif;
  int idx;

  struct iwl_mvm_phy_ctxt* phyctxt;

  uint16_t ids[MAX_MACS_IN_BINDING];
  uint16_t colors[MAX_MACS_IN_BINDING];
};

static zx_status_t iwl_mvm_binding_cmd(struct iwl_mvm* mvm, uint32_t action,
                                       struct iwl_mvm_iface_iterator_data* data) {
  struct iwl_binding_cmd cmd;
  struct iwl_mvm_phy_ctxt* phyctxt = data->phyctxt;
  int i;
  int size;

  memset(&cmd, 0, sizeof(cmd));

  if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT)) {
    size = sizeof(cmd);
    if (iwl_mvm_get_channel_band(phyctxt->chandef.primary) == WLAN_INFO_BAND_TWO_GHZ ||
        !iwl_mvm_is_cdb_supported(mvm)) {
      cmd.lmac_id = cpu_to_le32(IWL_LMAC_24G_INDEX);
    } else {
      cmd.lmac_id = cpu_to_le32(IWL_LMAC_5G_INDEX);
    }
  } else {
    size = IWL_BINDING_CMD_SIZE_V1;
  }

  cmd.id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));
  cmd.action = cpu_to_le32(action);
  cmd.phy = cpu_to_le32(FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));

  for (i = 0; i < MAX_MACS_IN_BINDING; i++) {
    cmd.macs[i] = cpu_to_le32(FW_CTXT_INVALID);
  }
  for (i = 0; i < data->idx; i++) {
    cmd.macs[i] = cpu_to_le32(FW_CMD_ID_AND_COLOR(data->ids[i], data->colors[i]));
  }

  uint32_t status = 0;
  zx_status_t ret = iwl_mvm_send_cmd_pdu_status(mvm, BINDING_CONTEXT_CMD, size, &cmd, &status);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "Failed to send binding (action:%d): %d\n", action, ret);
    return ret;
  }

  if (status) {
    IWL_ERR(mvm, "Binding command failed: %u\n", status);
    ret = ZX_ERR_IO;
  }

  return ret;
}

static void iwl_mvm_iface_iterator(void* _data, struct iwl_mvm_vif* mvmvif) {
  struct iwl_mvm_iface_iterator_data* data = _data;

  if (mvmvif == data->ignore_vif) {
    return;
  }

  if (mvmvif->phy_ctxt != data->phyctxt) {
    return;
  }

  if (WARN_ON_ONCE(data->idx >= MAX_MACS_IN_BINDING)) {
    return;
  }

  data->ids[data->idx] = mvmvif->id;
  data->colors[data->idx] = mvmvif->color;
  data->idx++;
}

static zx_status_t iwl_mvm_binding_update(struct iwl_mvm_vif* mvmvif,
                                          struct iwl_mvm_phy_ctxt* phyctxt, bool add) {
  struct iwl_mvm_iface_iterator_data data = {
      .ignore_vif = mvmvif,
      .phyctxt = phyctxt,
  };
  uint32_t action = FW_CTXT_ACTION_MODIFY;

  iwl_assert_lock_held(&mvmvif->mvm->mutex);

  ieee80211_iterate_active_interfaces_atomic(mvmvif->mvm, iwl_mvm_iface_iterator, &data);

  /*
   * If there are no other interfaces yet we
   * need to create a new binding.
   */
  if (data.idx == 0) {
    if (add) {
      action = FW_CTXT_ACTION_ADD;
    } else {
      action = FW_CTXT_ACTION_REMOVE;
    }
  }

  if (add) {
    if (data.idx >= MAX_MACS_IN_BINDING) {
      IWL_WARN(mvmvif, "cannot add. reached max # of binding: %d >= %d\n", data.idx,
               MAX_MACS_IN_BINDING);
      return ZX_ERR_OUT_OF_RANGE;
    }

    data.ids[data.idx] = mvmvif->id;
    data.colors[data.idx] = mvmvif->color;
    data.idx++;
  }

  return iwl_mvm_binding_cmd(mvmvif->mvm, action, &data);
}

zx_status_t iwl_mvm_binding_add_vif(struct iwl_mvm_vif* mvmvif) {
  if (!mvmvif->phy_ctxt) {
    IWL_ERR(mvmvif, "%s(): mvmvif->phy_ctxt is NULL\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

#if 0   // NEEDS_PORTING
  /*
   * Update SF - Disable if needed. if this fails, SF might still be on
   * while many macs are bound, which is forbidden - so fail the binding.
   */
  if (iwl_mvm_sf_update(mvm, vif, false)) {
    return -EINVAL;
  }
#endif  // NEEDS_PORTING

  return iwl_mvm_binding_update(mvmvif, mvmvif->phy_ctxt, true);
}

zx_status_t iwl_mvm_binding_remove_vif(struct iwl_mvm_vif* mvmvif) {
  if (!mvmvif->phy_ctxt) {
    IWL_ERR(mvmvif, "phy_ctxt cannot be null\n");
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t ret = iwl_mvm_binding_update(mvmvif, mvmvif->phy_ctxt, false);

#if 0   // NEEDS_PORTING
  if (ret != ZX_OK)
    if (iwl_mvm_sf_update(mvm, vif, true)) {
      IWL_ERR(mvm, "Failed to update SF state\n");
    }
#endif  // NEEDS_PORTING

  return ret;
}
