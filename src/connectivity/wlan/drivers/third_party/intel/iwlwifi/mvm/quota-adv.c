/******************************************************************************
 *
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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

#include <net/mac80211.h>

#include "constants.h"
#include "fw-api.h"
#include "mvm.h"

/* all calculations are done in percentages, but the firmware wants
 * fractions of 128, so provide a conversion.
 */
static inline uint32_t iwl_mvm_quota_from_pct(uint32_t pct) {
  return (pct * IWL_MVM_MAX_QUOTA) / 100;
}

struct iwl_mvm_quota_iterator_data {
  struct ieee80211_vif* disabled_vif;
  /*
   * Normally, transferring pointers from inside the iteration
   * to outside is a bug, but all the code here is protected by
   * the mvm mutex, so nothing can be added/removed and race.
   */
  uint32_t num_active_macs;
  struct ieee80211_vif* vifs[NUM_MAC_INDEX_DRIVER];
  bool monitor;
};

static void iwl_mvm_quota_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_quota_iterator_data* data = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  uint32_t num_active_macs = data->num_active_macs;
  uint16_t id;

  /* reset tracked quota but otherwise skip interfaces being disabled */
  if (vif == data->disabled_vif) {
    goto out;
  }

  if (!mvmvif->phy_ctxt) {
    goto out;
  }

  /* currently, PHY ID == binding ID */
  id = mvmvif->phy_ctxt->id;

  /* need at least one binding per PHY */
  BUILD_BUG_ON(NUM_PHY_CTX > MAX_BINDINGS);

  if (WARN_ON_ONCE(id >= MAX_BINDINGS)) {
    goto out;
  }

  switch (vif->type) {
    case NL80211_IFTYPE_STATION:
      if (vif->bss_conf.assoc) {
        data->vifs[data->num_active_macs] = vif;
        data->num_active_macs++;
      }
      break;
    case NL80211_IFTYPE_AP:
    case NL80211_IFTYPE_ADHOC:
      if (mvmvif->ap_ibss_active) {
        data->vifs[data->num_active_macs] = vif;
        data->num_active_macs++;
      }
      break;
    case NL80211_IFTYPE_MONITOR:
      if (mvmvif->monitor_active) {
        data->vifs[data->num_active_macs] = vif;
        data->num_active_macs++;
        data->monitor = true;
      }
      break;
    case NL80211_IFTYPE_P2P_DEVICE:
    case NL80211_IFTYPE_NAN:
      break;
    default:
      WARN_ON_ONCE(1);
      break;
  }

out:
  /* If this interface isn't considered now always reset its
   * assigned quota so the next time it's considered it will
   * be handled properly.
   */
  if (num_active_macs == data->num_active_macs) {
    mvmvif->pct_quota = 0;
  }
}

static uint32_t iwl_mvm_next_quota(struct iwl_mvm* mvm, uint32_t usage, uint32_t alloc,
                                   uint32_t unused, uint32_t n_vifs) {
  uint32_t result;
  uint32_t m;

  IWL_DEBUG_QUOTA(mvm, "next_quota usage=%d, alloc=%d, unused=%d, n_vifs=%d\n", usage, alloc,
                  unused, n_vifs);

  if (alloc == 0) {
    IWL_DEBUG_QUOTA(mvm, "new interface - next=%d\n", IWL_MVM_DYNQUOTA_START_PERCENT);
    return IWL_MVM_DYNQUOTA_START_PERCENT;
  }

  if (usage > IWL_MVM_DYNQUOTA_HIGH_WM_PERCENT) {
    if (unused > 0) {
      result = alloc + (unused / n_vifs) + IWL_MVM_DYNQUOTA_INC_HIGH_PERCENT;
      IWL_DEBUG_QUOTA(mvm, "high usage boost - next=%d\n", result);
      return result;
    }
    result = 100 / n_vifs;
    IWL_DEBUG_QUOTA(mvm, "high usage w/o boost - next=%d\n", result);
    return result;
  }

  if (usage > IWL_MVM_DYNQUOTA_LOW_WM_PERCENT) {
    IWL_DEBUG_QUOTA(mvm, "medium usage - next=%d\n", alloc);
    return alloc;
  }

  m = min_t(uint32_t, IWL_MVM_DYNQUOTA_MIN_PERCENT + unused / n_vifs,
            IWL_MVM_DYNQUOTA_LOW_WM_PERCENT - usage);
  if (alloc > IWL_MVM_DYNQUOTA_MIN_PERCENT + m) {
    result = alloc - m;
  } else {
    result = IWL_MVM_DYNQUOTA_MIN_PERCENT;
  }
  IWL_DEBUG_QUOTA(mvm, "low usage - next=%d\n", result);
  return result;
}

enum iwl_mvm_quota_result iwl_mvm_calculate_advanced_quotas(struct iwl_mvm* mvm,
                                                            struct ieee80211_vif* disabled_vif,
                                                            bool force_update,
                                                            struct iwl_time_quota_cmd* cmd) {
  int i, idx;
  struct iwl_mvm_quota_iterator_data data = {
      .disabled_vif = disabled_vif,
  };
  struct iwl_time_quota_data* quota;
  uint32_t usage[NUM_MAC_INDEX_DRIVER];
  uint32_t unused;
  uint32_t total;
  int n_lowlat = 0;
  int iter;
  uint32_t new_quota[NUM_MAC_INDEX_DRIVER];
  bool significant_change = false;

  iwl_assert_lock_held(&mvm->mutex);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (IWL_MVM_DYNQUOTA_DISABLED) {
    return IWL_MVM_QUOTA_ERROR;
  }
#endif

  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                             iwl_mvm_quota_iterator, &data);

  if (!data.num_active_macs) {
    return IWL_MVM_QUOTA_ERROR;
  }

  if (WARN_ON(data.num_active_macs * IWL_MVM_DYNQUOTA_MIN_PERCENT > 100)) {
    return IWL_MVM_QUOTA_ERROR;
  }

  if (data.monitor) {
    WARN(data.num_active_macs != 1, "unexpectedly have %d MACs active despite monitor\n",
         data.num_active_macs);
    return IWL_MVM_QUOTA_ERROR;
  }

  unused = 0;

  spin_lock_bh(&mvm->tcm.lock);
  for (i = 0; i < data.num_active_macs; i++) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);
    int id = mvmvif->id;

    if (!mvmvif->pct_quota) {
      continue;
    }

    /* load percentage of the total elapsed time */
    usage[id] = iwl_mvm_tcm_load_percentage(mvm->tcm.result.airtime[id], mvm->tcm.result.elapsed);
    /* expressed as percentage of the assigned quota */
    usage[id] = (100 * usage[id]) / mvmvif->pct_quota;
    /* can be > 1 when sharing channel contexts */
    usage[id] = min_t(uint32_t, 100, usage[id]);
#ifdef CPTCFG_IWLWIFI_DEBUGFS
    mvm->quotadbg.quota_used[id] = usage[id];
#endif
    unused += (mvmvif->pct_quota * (100 - usage[id])) / 100;
  }
  spin_unlock_bh(&mvm->tcm.lock);

  total = 0;

  for (i = 0; i < data.num_active_macs; i++) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);
    int id = mvmvif->id;

    new_quota[id] =
        iwl_mvm_next_quota(mvm, usage[id], mvmvif->pct_quota, unused, data.num_active_macs);

    if (iwl_mvm_vif_low_latency(mvmvif)) {
      uint32_t ll_min;

      switch (ieee80211_vif_type_p2p(data.vifs[i])) {
        case NL80211_IFTYPE_P2P_CLIENT:
          ll_min = IWL_MVM_LOWLAT_QUOTA_MIN_PCT_P2PCLIENT;
          break;
        case NL80211_IFTYPE_P2P_GO:
        case NL80211_IFTYPE_AP:
          ll_min = IWL_MVM_LOWLAT_QUOTA_MIN_PCT_P2PGO;
          break;
        default:
          ll_min = IWL_MVM_LOWLAT_QUOTA_MIN_PERCENT;
          break;
      }
      new_quota[id] = max_t(uint32_t, ll_min, new_quota[id]);
      n_lowlat++;
    }
    total += new_quota[id];
  }

  /* take away evenly if > 100 */
  if (total > 100) {
    for (i = 0; i < data.num_active_macs; i++) {
      struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

      new_quota[mvmvif->id] = (new_quota[mvmvif->id] * 100) / total;
    }

    total = 0;
    for (i = 0; i < data.num_active_macs; i++) {
      struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

      total += new_quota[mvmvif->id];
    }
  }

  /* distribute the remainder if any - preferably to low-latency */
  i = 0;
  while (total < 100) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

    if (n_lowlat == 0 || iwl_mvm_vif_low_latency(mvmvif)) {
      new_quota[mvmvif->id]++;
      total++;
    }
    i = (i + 1) % data.num_active_macs;
  }

  /* ensure minimum allocation for each */
  total = 0;
  for (i = 0; i < data.num_active_macs; i++) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

    if (new_quota[mvmvif->id] < IWL_MVM_DYNQUOTA_MIN_PERCENT) {
      new_quota[mvmvif->id] = IWL_MVM_DYNQUOTA_MIN_PERCENT;
    }
    total += new_quota[mvmvif->id];
  }

  iter = 0;
  i = 0;
  while (total > 100 && iter < 100) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

    if (new_quota[mvmvif->id] > IWL_MVM_DYNQUOTA_MIN_PERCENT) {
      new_quota[mvmvif->id]--;
      total--;
    }
    i = (i + 1) % data.num_active_macs;
  }

  if (WARN_ON(iter >= 100)) {
    return IWL_MVM_QUOTA_ERROR;
  }

  for (i = 0; i < data.num_active_macs; i++) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);

    if (abs((int32_t)new_quota[mvmvif->id] - (int32_t)mvmvif->pct_quota) >
        IWL_MVM_QUOTA_THRESHOLD) {
      significant_change = true;
      mvmvif->pct_quota = new_quota[mvmvif->id];
    }
  }

  if (!significant_change && !force_update) {
    return IWL_MVM_QUOTA_SKIP;
  }

  /* prepare command to upload to device */
  for (i = 0; i < MAX_BINDINGS; i++) {
    quota = iwl_mvm_quota_cmd_get_quota(mvm, cmd, i);
    quota->id_and_color = cpu_to_le32(FW_CTXT_INVALID);
    quota->quota = cpu_to_le32(0);
    quota->max_duration = cpu_to_le32(0);
  }

  for (i = 0; i < data.num_active_macs; i++) {
    struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(data.vifs[i]);
    uint32_t color;

    /* we always set binding id/color == phy id/color */
    idx = mvmvif->phy_ctxt->id;
    color = mvmvif->phy_ctxt->color;

    if (WARN_ON_ONCE(idx >= MAX_BINDINGS)) {
      continue;
    }

    quota = iwl_mvm_quota_cmd_get_quota(mvm, cmd, idx);
    quota->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(idx, color));
    le32_add_cpu(&quota->quota, iwl_mvm_quota_from_pct(mvmvif->pct_quota));
  }

  /* due to the 100/128 mix between the calculation and the firmware,
   * we can get errors. Distribute those among the bindings.
   */
  total = 0;
  for (i = 0; i < MAX_BINDINGS; i++) {
    quota = iwl_mvm_quota_cmd_get_quota(mvm, cmd, i);
    total += le32_to_cpu(quota->quota);
  }
  if (WARN(total > IWL_MVM_MAX_QUOTA, "total (%d) too big\n", total)) {
    memset(cmd, 0, sizeof(*cmd));
    return IWL_MVM_QUOTA_ERROR;
  }

  idx = 0;
  while (total < IWL_MVM_MAX_QUOTA) {
    quota = iwl_mvm_quota_cmd_get_quota(mvm, cmd, idx);
    if (quota->quota) {
      le32_add_cpu(&quota->quota, 1);
      total += 1;
    }
    idx = (idx + 1) % MAX_BINDINGS;
  }

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  mvm->quotadbg.cmd = *cmd;
  mvm->quotadbg.last_update = jiffies;
#endif

  return IWL_MVM_QUOTA_OK;
}

#ifdef CPTCFG_IWLWIFI_DEBUGFS
struct quota_mac_data {
  struct {
    enum nl80211_iftype iftype;
    int32_t phy_id;
    bool low_latency;
    uint32_t quota;
  } macs[NUM_MAC_INDEX_DRIVER];
};

static void iwl_mvm_quota_dbg_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct quota_mac_data* md = _data;

  if (WARN_ON(mvmvif->id >= NUM_MAC_INDEX_DRIVER)) {
    return;
  }

  md->macs[mvmvif->id].iftype = ieee80211_iftype_p2p(vif->type, vif->p2p);
  md->macs[mvmvif->id].phy_id = mvmvif->phy_ctxt ? mvmvif->phy_ctxt->id : -1;
  md->macs[mvmvif->id].low_latency = iwl_mvm_vif_low_latency(mvmvif);
  md->macs[mvmvif->id].quota = mvmvif->pct_quota;
}

ssize_t iwl_dbgfs_quota_status_read(struct file* file, char __user* user_buf, size_t count,
                                    loff_t* ppos) {
  struct iwl_mvm* mvm = file->private_data;
  static const char* iftypes[NUM_NL80211_IFTYPES] = {
      "<unused>", "ADHOC",      "STATION",    "AP",     "AP_VLAN",    "WDS",
      "MONITOR",  "MESH_POINT", "P2P_CLIENT", "P2P_GO", "P2P_DEVICE",
  };
  struct quota_mac_data md = {};
  char* buf = (void*)get_zeroed_page(GFP_KERNEL);
  size_t pos = 0, bufsz = PAGE_SIZE;
  ssize_t ret;
  int i;

  if (!buf) {
    return -ENOMEM;
  }

  mutex_lock(&mvm->mutex);
  ieee80211_iterate_active_interfaces(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                      iwl_mvm_quota_dbg_iterator, &md);
  mutex_unlock(&mvm->mutex);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
  if (IWL_MVM_DYNQUOTA_DISABLED)
    pos += scnprintf(buf + pos, bufsz - pos,
                     "dynamic quota is disabled - values marked * are incorrect!\n\n");
#endif

#define DESC_ROW 20
#define VAL_ROW 11
#define ADD_ROW(name, valfmt, val)                                               \
  do {                                                                           \
    int _m;                                                                      \
    pos += scnprintf(buf + pos, bufsz - pos, "%-*s |", DESC_ROW, name);          \
    for (_m = 0; _m < NUM_MAC_INDEX_DRIVER; _m++)                                \
      pos += scnprintf(buf + pos, bufsz - pos, " %*" valfmt " |", VAL_ROW, val); \
    pos += scnprintf(buf + pos, bufsz - pos, "\n");                              \
  } while (0)

  ADD_ROW("MAC data", "d", _m);
  pos +=
      scnprintf(buf + pos, bufsz - pos,
                "------------------------------------------------------------------------------\n");
  ADD_ROW("iftype", "s", iftypes[md.macs[_m].iftype]);
  ADD_ROW("PHY index", "d", md.macs[_m].phy_id);
  ADD_ROW("channel busy time", "d", mvm->tcm.result.airtime[_m]);
  ADD_ROW("low latency", "d", md.macs[_m].low_latency);
  ADD_ROW("*quota (%)", "d", md.macs[_m].quota);
  ADD_ROW("*quota used (%)", "d", mvm->quotadbg.quota_used[_m]);
  pos += scnprintf(buf + pos, bufsz - pos, "\n");

  pos += scnprintf(buf + pos, bufsz - pos, "elapsed time since update: %d ms\n",
                   jiffies_to_msecs(jiffies - mvm->quotadbg.last_update));
  pos += scnprintf(buf + pos, bufsz - pos, "elapsed in last complete TCM period: %d ms\n",
                   mvm->tcm.result.elapsed);
  pos += scnprintf(buf + pos, bufsz - pos, "elapsed in current open TCM period: %d ms\n",
                   jiffies_to_msecs(jiffies - mvm->tcm.ts));

  pos += scnprintf(buf + pos, bufsz - pos, "\n*last calculated quota cmd:\n");
  for (i = 0; i < MAX_BINDINGS; i++) {
    struct iwl_time_quota_data* q = iwl_mvm_quota_cmd_get_quota(mvm, &mvm->quotadbg.cmd, i);
    pos += scnprintf(buf + pos, bufsz - pos,
                     "binding #%d |  Quota:% 4d | MaxDuration:% 4d | id_and_color: 0x%.8x\n", i,
                     le32_to_cpu(q->quota), le32_to_cpu(q->max_duration),
                     le32_to_cpu(q->id_and_color));
  }

  pos += scnprintf(buf + pos, bufsz - pos, "\ncurrent quota sent to firmware:\n");
  for (i = 0; i < MAX_BINDINGS; i++) {
    struct iwl_time_quota_data* q = iwl_mvm_quota_cmd_get_quota(mvm, &mvm->quotadbg.cmd, i);
    pos += scnprintf(buf + pos, bufsz - pos,
                     "binding #%d |  Quota:% 4d | MaxDuration:% 4d | id_and_color: 0x%.8x\n", i,
                     le32_to_cpu(q->quota), le32_to_cpu(q->max_duration),
                     le32_to_cpu(q->id_and_color));
  }

  ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
  free_page((unsigned long)buf);
  return ret;
}
#endif /* CPTCFG_IWLWIFI_DEBUGFS */
