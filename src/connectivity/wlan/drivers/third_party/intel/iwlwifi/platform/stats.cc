// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/time.h>  // for async_now()
#include <zircon/time.h>

#include <mutex>

#include <wlan/common/ieee80211.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#define IWL_STATS_INTERVAL ZX_SEC(20)

static const char* descs[] = {"ints", "fw_cmd",    "be",       "bc",      "mc",
                              "uni",  "from_mlme", "data->fw", "cmd->fw", "txq_drop",
                              "buf",  "drop", "to"};

struct iwl_stats_data {
  async_dispatcher_t* dispatcher;
  async_task_t task;

  int8_t last_rssi_dbm;
  uint32_t last_data_rate;

  zx_duration_t max_isr_duration;
  zx_duration_t total_isr_duration;

  size_t counters[IWL_STATS_CNT_MAX];
};

static struct iwl_stats_data stats_data;
static std::mutex mutex_lock;

static void iwl_stats_schedule_next(zx_duration_t interval) {
  async_cancel_task(stats_data.dispatcher, &stats_data.task);
  stats_data.task.deadline = interval + async_now(stats_data.dispatcher);
  async_post_task(stats_data.dispatcher, &stats_data.task);
}

void iwl_stats_report_wk(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
  const std::lock_guard<std::mutex> lock(mutex_lock);

  // TODO(fxb/101542): better debug info for bug triage.
  // clang-format off
  zxlogf(INFO,
         "rssi:%d rate:%u [%s:%zu %s:%zu (%s:%zu,%s:%zu,%s:%zu,%s:%zu)] "
         "[%s:%zu %s:%zu %s:%zu] [%s:%zu] reorder:[%s:%zu %s:%zu %s:%zu]",
      stats_data.last_rssi_dbm, stats_data.last_data_rate,
      descs[IWL_STATS_CNT_INTS_FROM_FW], stats_data.counters[IWL_STATS_CNT_INTS_FROM_FW],
      descs[IWL_STATS_CNT_CMD_FROM_FW], stats_data.counters[IWL_STATS_CNT_CMD_FROM_FW],
      descs[IWL_STATS_CNT_BCAST_TO_MLME], stats_data.counters[IWL_STATS_CNT_BCAST_TO_MLME],
      descs[IWL_STATS_CNT_MCAST_TO_MLME], stats_data.counters[IWL_STATS_CNT_MCAST_TO_MLME],
      descs[IWL_STATS_CNT_UNICAST_TO_MLME], stats_data.counters[IWL_STATS_CNT_UNICAST_TO_MLME],
      descs[IWL_STATS_CNT_BEACON_TO_MLME], stats_data.counters[IWL_STATS_CNT_BEACON_TO_MLME],
      descs[IWL_STATS_CNT_DATA_FROM_MLME], stats_data.counters[IWL_STATS_CNT_DATA_FROM_MLME],
      descs[IWL_STATS_CNT_DATA_TO_FW], stats_data.counters[IWL_STATS_CNT_DATA_TO_FW],
      descs[IWL_STATS_CNT_CMD_TO_FW], stats_data.counters[IWL_STATS_CNT_CMD_TO_FW],
      descs[IWL_STATS_CNT_TXQ_DROP], stats_data.counters[IWL_STATS_CNT_TXQ_DROP],
      descs[IWL_STATS_CNT_FRAMES_BUFFERED], stats_data.counters[IWL_STATS_CNT_FRAMES_BUFFERED],
      descs[IWL_STATS_CNT_REORDER_DROP], stats_data.counters[IWL_STATS_CNT_REORDER_DROP],
      descs[IWL_STATS_CNT_REORDER_TIMEOUT], stats_data.counters[IWL_STATS_CNT_REORDER_TIMEOUT]);
  // clang-format on

  uint64_t avg_isr_duration = 0;
  if (stats_data.counters[IWL_STATS_CNT_INTS_FROM_FW]) {
    avg_isr_duration =
        stats_data.total_isr_duration / stats_data.counters[IWL_STATS_CNT_INTS_FROM_FW];
  }

  zxlogf(INFO, "rx isr: avg:%zuns, max:%zuns", avg_isr_duration, stats_data.max_isr_duration);

  iwl_stats_schedule_next(IWL_STATS_INTERVAL);
}

void iwl_stats_init(async_dispatcher_t* dispatcher) {
  const std::lock_guard<std::mutex> lock(mutex_lock);

  memset(&stats_data, 0, sizeof(stats_data));

  stats_data.dispatcher = dispatcher;
  stats_data.task.handler = &iwl_stats_report_wk;
}

void iwl_stats_start_reporting(void) { iwl_stats_schedule_next(ZX_SEC(0)); }

void iwl_stats_update_last_rssi(int8_t rssi_dbm) {
  const std::lock_guard<std::mutex> lock(mutex_lock);

  stats_data.last_rssi_dbm = rssi_dbm;
}

void iwl_stats_update_date_rate(uint32_t data_rate) {
  const std::lock_guard<std::mutex> lock(mutex_lock);
  stats_data.last_data_rate = data_rate;
}

void iwl_stats_update_rx_isr_duration(zx_duration_t isr_duration) {
  const std::lock_guard<std::mutex> lock(mutex_lock);
  stats_data.max_isr_duration = std::max(stats_data.max_isr_duration, isr_duration);
  stats_data.total_isr_duration += isr_duration;
}

size_t iwl_stats_read(enum iwl_stats_counter_index index) {
  ZX_ASSERT(index < IWL_STATS_CNT_MAX);

  const std::lock_guard<std::mutex> lock(mutex_lock);
  return stats_data.counters[index];
}

void iwl_stats_inc(enum iwl_stats_counter_index index) {
  ZX_ASSERT(index < IWL_STATS_CNT_MAX);

  const std::lock_guard<std::mutex> lock(mutex_lock);
  stats_data.counters[index]++;
}

void iwl_stats_analyze_rx(const wlan_rx_packet_t* pkt) {
  struct ieee80211_frame_header* fh = (struct ieee80211_frame_header*)pkt->mac_frame_buffer;
  uint16_t frame_ctrl = fh->frame_ctrl;
  uint8_t* addr1 = fh->addr1;

  if (is_broadcast_addr(addr1)) {
    if (frame_ctrl == 0x0080) {
      iwl_stats_inc(IWL_STATS_CNT_BEACON_TO_MLME);
    } else {
      iwl_stats_inc(IWL_STATS_CNT_BCAST_TO_MLME);
    }
  } else if (is_multicast_addr(addr1)) {
    iwl_stats_inc(IWL_STATS_CNT_MCAST_TO_MLME);
  } else {
    iwl_stats_inc(IWL_STATS_CNT_UNICAST_TO_MLME);
  }
}
