/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/notif-wait.h"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"

void iwl_notification_wait_init(struct iwl_notif_wait_data* notif_wait) {
  mtx_init(&notif_wait->notif_wait_lock, mtx_plain);
  list_initialize(&notif_wait->notif_waits);
  sync_completion_reset(&notif_wait->notif_waitq);
}

bool iwl_notification_wait(struct iwl_notif_wait_data* notif_wait, struct iwl_rx_packet* pkt) {
  bool triggered = false;

  if (!list_is_empty(&notif_wait->notif_waits)) {
    struct iwl_notification_wait* w;

    mtx_lock(&notif_wait->notif_wait_lock);
    list_for_every_entry (&notif_wait->notif_waits, w, struct iwl_notification_wait, list) {
      int i;
      bool found = false;

      /*
       * If it already finished (triggered) or has been
       * aborted then don't evaluate it again to avoid races,
       * Otherwise the function could be called again even
       * though it returned true before
       */
      if (w->triggered || w->aborted) {
        continue;
      }

      for (i = 0; i < w->n_cmds; i++) {
        uint16_t rec_id = WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd);

        if (w->cmds[i] == rec_id ||
            (!iwl_cmd_groupid(w->cmds[i]) && DEF_ID(w->cmds[i]) == rec_id)) {
          found = true;
          break;
        }
      }
      if (!found) {
        continue;
      }

      if (!w->fn || w->fn(notif_wait, pkt, w->fn_data)) {
        w->triggered = true;
        triggered = true;
      }
    }
    mtx_unlock(&notif_wait->notif_wait_lock);
  }

  return triggered;
}

void iwl_abort_notification_waits(struct iwl_notif_wait_data* notif_wait) {
  struct iwl_notification_wait* wait_entry;

  mtx_lock(&notif_wait->notif_wait_lock);
  list_for_every_entry (&notif_wait->notif_waits, wait_entry, struct iwl_notification_wait, list) {
    wait_entry->aborted = true;
  }
  mtx_unlock(&notif_wait->notif_wait_lock);

  sync_completion_signal(&notif_wait->notif_waitq);
}

void iwl_init_notification_wait(struct iwl_notif_wait_data* notif_wait,
                                struct iwl_notification_wait* wait_entry, const uint16_t* cmds,
                                int n_cmds,
                                bool (*fn)(struct iwl_notif_wait_data* notif_wait,
                                           struct iwl_rx_packet* pkt, void* data),
                                void* fn_data) {
  if (WARN_ON(n_cmds > MAX_NOTIF_CMDS)) {
    n_cmds = MAX_NOTIF_CMDS;
  }

  wait_entry->fn = fn;
  wait_entry->fn_data = fn_data;
  wait_entry->n_cmds = n_cmds;
  memcpy(wait_entry->cmds, cmds, n_cmds * sizeof(uint16_t));
  wait_entry->triggered = false;
  wait_entry->aborted = false;

  mtx_lock(&notif_wait->notif_wait_lock);
  list_add_tail(&notif_wait->notif_waits, &wait_entry->list);
  mtx_unlock(&notif_wait->notif_wait_lock);
}

void iwl_remove_notification(struct iwl_notif_wait_data* notif_wait,
                             struct iwl_notification_wait* wait_entry) {
  mtx_lock(&notif_wait->notif_wait_lock);
  list_delete(&wait_entry->list);
  mtx_unlock(&notif_wait->notif_wait_lock);
}

zx_status_t iwl_wait_notification(struct iwl_notif_wait_data* notif_wait,
                                  struct iwl_notification_wait* wait_entry, zx_duration_t timeout) {
  zx_status_t ret;

  ret = sync_completion_wait(&notif_wait->notif_waitq, timeout);

  // Clear the signal state.
  //
  // In the signaled case, the next call to sync_completion_wait() will return immediately without
  // waiting (since sync_completion_t is still in signaled state). So clear it.
  //
  sync_completion_reset(&notif_wait->notif_waitq);

  iwl_remove_notification(notif_wait, wait_entry);

  if (wait_entry->aborted) {
    return ZX_ERR_CANCELED;
  }

  return ret;
}
