/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
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

#include <lib/async/time.h>
#include <zircon/status.h>

#include <ddk/hw/wlan/ieee80211.h>

#if 0  // NEEDS_PORTING
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/tx.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#endif  // NEEDS_PORTING
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-scd.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"

#if 0  // NEEDS_PORTING
#define IWL_TX_CRC_SIZE 4
#define IWL_TX_DELIMITER_SIZE 4
#endif  // NEEDS_PORTING

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 ***************************************************/

int iwl_queue_space(struct iwl_trans* trans, const struct iwl_txq* q) {
  uint16_t max;
  uint16_t used;

  /*
   * To avoid ambiguity between empty and completely full queues, there
   * should always be less than max_tfd_queue_size elements in the queue.
   * If q->n_window is smaller than max_tfd_queue_size, there is no need
   * to reserve any queue entries for this purpose.
   */
  if (q->n_window < trans->cfg->base_params->max_tfd_queue_size) {
    max = q->n_window;
  } else {
    max = trans->cfg->base_params->max_tfd_queue_size - 1;
  }

  /*
   * max_tfd_queue_size is a power of 2, so the following is equivalent to
   * modulo by max_tfd_queue_size and is well defined.
   */
  used = (q->write_ptr - q->read_ptr) & (trans->cfg->base_params->max_tfd_queue_size - 1);

  if (WARN_ON(used > max)) {
    return 0;
  }

  return max - used;
}

/*
 * iwl_queue_init - Initialize queue's high/low-water and read/write indexes
 */
static zx_status_t iwl_queue_init(struct iwl_txq* q, uint16_t slots_num) {
  q->n_window = slots_num;

  // slots_num must be power-of-two size, otherwise iwl_pcie_get_cmd_index is broken.
  if (WARN_ON(slots_num <= 0 || (slots_num & (slots_num - 1)) != 0)) {
    return ZX_ERR_INVALID_ARGS;
  }

  q->low_mark = q->n_window / 4;
  if (q->low_mark < 4) {
    q->low_mark = 4;
  }

  q->high_mark = q->n_window / 8;
  if (q->high_mark < 2) {
    q->high_mark = 2;
  }

  q->write_ptr = 0;
  q->read_ptr = 0;

  return ZX_OK;
}

zx_status_t iwl_pcie_alloc_dma_ptr(struct iwl_trans* trans, struct iwl_dma_ptr* ptr, size_t size) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  zx_status_t status =
      io_buffer_init(&ptr->io_buf, trans_pcie->bti, size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    return status;
  }

  ptr->size = size;
  return ZX_OK;
}

void iwl_pcie_free_dma_ptr(struct iwl_trans* trans, struct iwl_dma_ptr* ptr) {
  io_buffer_release(&ptr->io_buf);
}

static void iwl_pcie_txq_stuck_timer(async_dispatcher_t* dispatcher, async_task_t* task,
                                     zx_status_t status) {
  if (status != ZX_OK) {
    // This indicates that the dispatcher was shut down, in which case there's nothing for us to do.
    return;
  }

  struct iwlwifi_timer_info* timer = containerof(task, struct iwlwifi_timer_info, task);
  struct iwl_txq* txq = containerof(timer, struct iwl_txq, stuck_timer);

  mtx_lock(&txq->lock);
  /* check if triggered erroneously */
  if (txq->read_ptr == txq->write_ptr) {
    mtx_unlock(&txq->lock);
    sync_completion_signal(&timer->finished);
    return;
  }
  mtx_unlock(&txq->lock);

#if 0  // NEEDS_PORTING
  struct iwl_trans_pcie* trans_pcie = txq->trans_pcie;
  struct iwl_trans* trans = iwl_trans_pcie_get_trans(trans_pcie);
  iwl_trans_pcie_log_scd_error(trans, txq);

  iwl_force_nmi(trans);
#endif  // NEEDS_PORTING

  sync_completion_signal(&timer->finished);
}

void iwlwifi_timer_init(struct iwl_trans* trans, struct iwlwifi_timer_info* timer) {
  timer->dispatcher = async_loop_get_dispatcher(trans->loop);

  // Initialize the completion to signaled so that if the timer is stopped before being set then
  // waiting on |finished| doesn't block.
  timer->finished = SYNC_COMPLETION_INIT;
  sync_completion_signal(&timer->finished);

  timer->task.state = (async_state_t)ASYNC_STATE_INIT;
  timer->task.handler = iwl_pcie_txq_stuck_timer;
  mtx_init(&timer->lock, mtx_plain);
}

void iwlwifi_timer_set(struct iwlwifi_timer_info* timer, zx_duration_t delay) {
  mtx_lock(&timer->lock);
  async_cancel_task(timer->dispatcher, &timer->task);
  timer->task.deadline = zx_time_add_duration(async_now(timer->dispatcher), delay);
  sync_completion_reset(&timer->finished);
  async_post_task(timer->dispatcher, &timer->task);
  mtx_unlock(&timer->lock);
}

void iwlwifi_timer_stop(struct iwlwifi_timer_info* timer) {
  mtx_lock(&timer->lock);
  zx_status_t status = async_cancel_task(timer->dispatcher, &timer->task);
  mtx_unlock(&timer->lock);

  // If we failed to cancel the task then it might already be running, so we wait for it to finish.
  // If the timer has not been set, or already finished then this will not block.
  if (status == ZX_ERR_NOT_FOUND) {
    sync_completion_wait(&timer->finished, ZX_TIME_INFINITE);
  }
}

#if 0  // NEEDS_PORTING
/*
 * iwl_pcie_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
static void iwl_pcie_txq_update_byte_cnt_tbl(struct iwl_trans* trans, struct iwl_txq* txq,
                                             uint16_t byte_cnt, int num_tbs) {
    struct iwlagn_scd_bc_tbl* scd_bc_tbl;
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    int write_ptr = txq->write_ptr;
    int txq_id = txq->id;
    uint8_t sec_ctl = 0;
    uint16_t len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
    __le16 bc_ent;
    struct iwl_tx_cmd* tx_cmd = (void*)txq->entries[txq->write_ptr].cmd->payload;
    uint8_t sta_id = tx_cmd->sta_id;

    scd_bc_tbl = trans_pcie->scd_bc_tbls.addr;

    sec_ctl = tx_cmd->sec_ctl;

    switch (sec_ctl & TX_CMD_SEC_MSK) {
    case TX_CMD_SEC_CCM:
        len += IEEE80211_CCMP_128_MIC_LEN;
        break;
    case TX_CMD_SEC_TKIP:
        len += IEEE80211_TKIP_ICV_LEN;
        break;
    case TX_CMD_SEC_WEP:
        len += IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN;
        break;
    }
    if (trans_pcie->bc_table_dword) { len = DIV_ROUND_UP(len, 4); }

    if (WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX)) { return; }

    bc_ent = cpu_to_le16(len | (sta_id << 12));

    scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

    if (write_ptr < TFD_QUEUE_SIZE_BC_DUP) {
        scd_bc_tbl[txq_id].tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] = bc_ent;
    }
}

static void iwl_pcie_txq_inval_byte_cnt_tbl(struct iwl_trans* trans, struct iwl_txq* txq) {
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct iwlagn_scd_bc_tbl* scd_bc_tbl = trans_pcie->scd_bc_tbls.addr;
    int txq_id = txq->id;
    int read_ptr = txq->read_ptr;
    uint8_t sta_id = 0;
    __le16 bc_ent;
    struct iwl_tx_cmd* tx_cmd = (void*)txq->entries[read_ptr].cmd->payload;

    WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

    if (txq_id != trans_pcie->cmd_queue) { sta_id = tx_cmd->sta_id; }

    bc_ent = cpu_to_le16(1 | (sta_id << 12));

    scd_bc_tbl[txq_id].tfd_offset[read_ptr] = bc_ent;

    if (read_ptr < TFD_QUEUE_SIZE_BC_DUP) {
        scd_bc_tbl[txq_id].tfd_offset[TFD_QUEUE_SIZE_MAX + read_ptr] = bc_ent;
    }
}
#endif  // NEEDS_PORTING

/*
 * iwl_pcie_txq_inc_wr_ptr - Send new write index to hardware
 */
static void iwl_pcie_txq_inc_wr_ptr(struct iwl_trans* trans, struct iwl_txq* txq) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t reg = 0;
  int txq_id = txq->id;

  iwl_assert_lock_held(&txq->lock);

  /*
   * explicitly wake up the NIC if:
   * 1. shadow registers aren't enabled
   * 2. NIC is woken up for CMD regardless of shadow outside this function
   * 3. there is a chance that the NIC is asleep
   */
  if (!trans->cfg->base_params->shadow_reg_enable && txq_id != trans_pcie->cmd_queue &&
      test_bit(STATUS_TPOWER_PMI, &trans->status)) {
    /*
     * wake up nic if it's powered down ...
     * uCode will wake up, and interrupt us again, so next
     * time we'll skip this part.
     */
    reg = iwl_read32(trans, CSR_UCODE_DRV_GP1);

    if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
      IWL_DEBUG_INFO(trans, "Tx queue %d requesting wakeup, GP1 = 0x%x\n", txq_id, reg);
      iwl_set_bit(trans, CSR_GP_CNTRL, BIT(trans->cfg->csr->flag_mac_access_req));
      txq->need_update = true;
      return;
    }
  }

  /*
   * if not in power-save mode, uCode will never sleep when we're
   * trying to tx (during RFKILL, we're not trying to tx).
   */
  IWL_DEBUG_TX(trans, "Q:%d WR: 0x%x\n", txq_id, txq->write_ptr);
  if (!txq->block) {
    iwl_write32(trans, HBUS_TARG_WRPTR, txq->write_ptr | (txq_id << 8));
  }
}

void iwl_pcie_txq_check_wrptrs(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int i;

  for (i = 0; i < trans->cfg->base_params->num_of_queues; i++) {
    struct iwl_txq* txq = trans_pcie->txq[i];

    if (!test_bit(i, trans_pcie->queue_used)) {
      continue;
    }

    mtx_lock(&txq->lock);
    if (txq->need_update) {
      iwl_pcie_txq_inc_wr_ptr(trans, txq);
      txq->need_update = false;
    }
    mtx_unlock(&txq->lock);
  }
}

static inline void iwl_pcie_tfd_set_tb(struct iwl_trans* trans, void* tfd, uint8_t idx,
                                       zx_paddr_t addr, uint16_t len) {
  struct iwl_tfd* tfd_fh = (void*)tfd;
  struct iwl_tfd_tb* tb = &tfd_fh->tbs[idx];

  uint16_t hi_n_len = len << 4;

  tb->lo = cpu_to_le32(addr);
  hi_n_len |= iwl_get_dma_hi_addr(addr);

  tb->hi_n_len = cpu_to_le16(hi_n_len);

  tfd_fh->num_tbs = idx + 1;
}

static inline uint8_t iwl_pcie_tfd_get_num_tbs(struct iwl_trans* trans, void* _tfd) {
  if (trans->cfg->use_tfh) {
    struct iwl_tfh_tfd* tfd = _tfd;

    return le16_to_cpu(tfd->num_tbs) & 0x1f;
  } else {
    struct iwl_tfd* tfd = _tfd;

    return tfd->num_tbs & 0x1f;
  }
}

//
// Since DMA addresses are manipulated by io_buffers, we don't do DMA unmap here. Instead,
// we update the TFD entry only (zero-ing num_tbs).
//
static void iwl_pcie_tfd_unmap(struct iwl_trans* trans, struct iwl_cmd_meta* meta,
                               struct iwl_txq* txq, int index) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int num_tbs;
  void* tfd = iwl_pcie_get_tfd(trans, txq, index);

  /* Sanity check on number of chunks */
  num_tbs = iwl_pcie_tfd_get_num_tbs(trans, tfd);

  if (num_tbs > trans_pcie->max_tbs) {
    IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
    /* @todo issue fatal error, it is quite serious situation */
    return;
  }

  // First TB is never freed - it's the bidirectional DMA data.
  // All first TBs (first_tb_bufs) will be freed at iwl_pcie_txq_free().

  if (trans->cfg->use_tfh) {
    struct iwl_tfh_tfd* tfd_fh = (void*)tfd;

    tfd_fh->num_tbs = 0;
  } else {
    struct iwl_tfd* tfd_fh = (void*)tfd;

    tfd_fh->num_tbs = 0;
  }
}

/*
 * iwl_pcie_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @trans - transport private data
 * @txq - tx queue
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void iwl_pcie_txq_free_tfd(struct iwl_trans* trans, struct iwl_txq* txq) {
  /* rd_ptr is bounded by TFD_QUEUE_SIZE_MAX and
   * idx is bounded by n_window
   */
  int rd_ptr = txq->read_ptr;
  int idx = iwl_pcie_get_cmd_index(txq, rd_ptr);

  iwl_assert_lock_held(&txq->lock);

  /* We have only q->n_window txq->entries, but we use
   * TFD_QUEUE_SIZE_MAX tfds
   */
  iwl_pcie_tfd_unmap(trans, &txq->entries[idx].meta, txq, rd_ptr);
}

// Build TX queue transfer descriptor.
__UNUSED static zx_status_t iwl_pcie_txq_build_tfd(struct iwl_trans* trans, struct iwl_txq* txq,
                                                   zx_paddr_t addr, uint16_t len, bool reset,
                                                   uint32_t* num_tbs) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  void* tfd;

  void* tfds = io_buffer_virt(&txq->tfds);
  tfd = tfds + trans_pcie->tfd_size * txq->write_ptr;

  if (reset) {
    memset(tfd, 0, trans_pcie->tfd_size);
  }

  *num_tbs = iwl_pcie_tfd_get_num_tbs(trans, tfd);

  /* Each TFD can point to a maximum max_tbs Tx buffers */
  if (*num_tbs >= trans_pcie->max_tbs) {
    IWL_ERR(trans, "Error can not send more than %d chunks\n", trans_pcie->max_tbs);
    return ZX_ERR_INVALID_ARGS;
  }

  if (addr & ~IWL_TX_DMA_MASK) {
    IWL_WARN(trans, "Unaligned address = %lx\n", addr);
    return ZX_ERR_INVALID_ARGS;
  }

  iwl_pcie_tfd_set_tb(trans, tfd, *num_tbs, addr, len);

  return ZX_OK;
}

zx_status_t iwl_pcie_txq_alloc(struct iwl_trans* trans, struct iwl_txq* txq, uint16_t slots_num,
                               bool cmd_queue) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (WARN_ON(txq->entries || io_buffer_is_valid(&txq->tfds))) {
    return ZX_ERR_BAD_STATE;
  }

  size_t tfd_sz = trans_pcie->tfd_size * trans->cfg->base_params->max_tfd_queue_size;
  if (trans->cfg->use_tfh) {
    tfd_sz = trans_pcie->tfd_size * slots_num;
  }

  iwlwifi_timer_init(trans, &txq->stuck_timer);
  txq->trans_pcie = trans_pcie;

  txq->n_window = slots_num;

  txq->entries = calloc(slots_num, sizeof(struct iwl_pcie_txq_entry));

  if (!txq->entries) {
    goto error;
  }

  if (cmd_queue) {
    for (int i = 0; i < slots_num; i++) {
      zx_status_t status =
          io_buffer_init(&txq->entries[i].cmd, trans_pcie->bti, sizeof(struct iwl_device_cmd),
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
      if (status != ZX_OK) {
        goto error;
      }
    }
  }

  // Circular buffer of transmit frame descriptors (TFDs), shared with device.
  zx_status_t status =
      io_buffer_init(&txq->tfds, trans_pcie->bti, tfd_sz, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    goto error;
  }
  txq->dma_addr = io_buffer_phys(&txq->tfds);

  BUILD_BUG_ON(IWL_FIRST_TB_SIZE_ALIGN != sizeof(struct iwl_pcie_first_tb_buf));

  size_t tb0_buf_sz = sizeof(struct iwl_pcie_first_tb_buf) * slots_num;

  status = io_buffer_init(&txq->first_tb_bufs, trans_pcie->bti, tb0_buf_sz,
                          IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    goto err_free_tfds;
  }

  return ZX_OK;

err_free_tfds:
  io_buffer_release(&txq->tfds);
error:
  if (txq->entries && cmd_queue) {
    for (int i = 0; i < slots_num; i++) {
      io_buffer_release(&txq->entries[i].cmd);
    }
  }
  free(txq->entries);
  txq->entries = NULL;

  return ZX_ERR_NO_MEMORY;
}

zx_status_t iwl_pcie_txq_init(struct iwl_trans* trans, struct iwl_txq* txq, uint16_t slots_num,
                              bool cmd_queue) {
  uint32_t tfd_queue_max_size = trans->cfg->base_params->max_tfd_queue_size;

  txq->need_update = false;

  // max_tfd_queue_size must be power-of-two size, otherwise iwl_queue_inc_wrap and
  // iwl_queue_dec_wrap are broken.
  if (WARN_ON(tfd_queue_max_size & (tfd_queue_max_size - 1))) {
    IWL_ERR(trans, "Max tfd queue size must be a power of two, but is %d", tfd_queue_max_size);
    return ZX_ERR_INVALID_ARGS;
  }

  /* Initialize queue's high/low-water marks, and head/tail indexes */
  zx_status_t status = iwl_queue_init(txq, slots_num);
  if (status != ZX_OK) {
    return status;
  }

  mtx_init(&txq->lock, mtx_plain);

#if 0  // NEEDS_PORTING
    if (cmd_queue) {
        static struct lock_class_key iwl_pcie_cmd_queue_lock_class;
        lockdep_set_class(&txq->lock, &iwl_pcie_cmd_queue_lock_class);
    }

    __skb_queue_head_init(&txq->overflow_q);
#endif  // NEEDS_PORTING

  return 0;
}

#if 0  // NEEDS_PORTING
void iwl_pcie_free_tso_page(struct iwl_trans_pcie* trans_pcie, struct sk_buff* skb) {
    struct page** page_ptr;

    page_ptr = (void*)((uint8_t*)skb->cb + trans_pcie->page_offs);

    if (*page_ptr) {
        __free_page(*page_ptr);
        *page_ptr = NULL;
    }
}
#endif  // NEEDS_PORTING

static void iwl_pcie_clear_cmd_in_flight(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  iwl_assert_lock_held(&trans_pcie->reg_lock);

  if (trans_pcie->ref_cmd_in_flight) {
    trans_pcie->ref_cmd_in_flight = false;
    IWL_DEBUG_RPM(trans, "clear ref_cmd_in_flight - unref\n");
    iwl_trans_unref(trans);
  }

  if (!trans->cfg->base_params->apmg_wake_up_wa) {
    return;
  }
  if (WARN_ON(!trans_pcie->cmd_hold_nic_awake)) {
    return;
  }

  trans_pcie->cmd_hold_nic_awake = false;
  __iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL, BIT(trans->cfg->csr->flag_mac_access_req));
}

//
// This function will traverse all remaining entries in a Tx queue.
//
// On the last entry, unref the device so that the power management code can put this device into
// power saving mode. In addition, for command queue, clear the ref_cmd_in_flight bit.
//
void iwl_pcie_txq_unmap(struct iwl_trans* trans, int txq_id) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[txq_id];

  mtx_lock(&txq->lock);
  while (txq->write_ptr != txq->read_ptr) {
    IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n", txq_id, txq->read_ptr);

    if (txq_id != trans_pcie->cmd_queue) {
#if 0  // NEEDS_PORTING
            iwl_pcie_free_tso_page(trans_pcie, skb);
#endif  // NEEDS_PORTING
    }
    iwl_pcie_txq_free_tfd(trans, txq);
    txq->read_ptr = iwl_queue_inc_wrap(trans, txq->read_ptr);

    if (txq->read_ptr == txq->write_ptr) {
      mtx_lock(&trans_pcie->reg_lock);
      if (txq_id != trans_pcie->cmd_queue) {
        IWL_DEBUG_RPM(trans, "Q %d - last tx freed\n", txq->id);
        iwl_trans_unref(trans);
      } else {
        iwl_pcie_clear_cmd_in_flight(trans);
      }
      mtx_unlock(&trans_pcie->reg_lock);
    }
  }

  mtx_unlock(&txq->lock);

  /* just in case - this queue may have been stopped */
  iwl_wake_queue(trans, txq);
}

/*
 * iwl_pcie_txq_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_pcie_txq_free(struct iwl_trans* trans, int txq_id) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  int i;

  if (WARN_ON(!txq)) {
    return;
  }

  iwl_pcie_txq_unmap(trans, txq_id);

  /* De-alloc array of command/tx buffers */
  if (txq_id == trans_pcie->cmd_queue) {
    for (i = 0; i < txq->n_window; i++) {
      io_buffer_release(&txq->entries[i].cmd);
      io_buffer_release(&txq->entries[i].dup_io_buf);
    }
  }

  /* De-alloc circular buffer of TFDs */
  io_buffer_release(&txq->tfds);
  txq->dma_addr = 0;
  io_buffer_release(&txq->first_tb_bufs);

  free(txq->entries);
  txq->entries = NULL;

  iwlwifi_timer_stop(&txq->stuck_timer);

  /* 0-fill queue descriptor structure */
  memset(txq, 0, sizeof(*txq));
}

void iwl_pcie_tx_start(struct iwl_trans* trans, uint32_t scd_base_addr) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int nq = trans->cfg->base_params->num_of_queues;
  int chan;
  uint32_t reg_val;
  int clear_dwords =
      (SCD_TRANS_TBL_OFFSET_QUEUE(nq) - SCD_CONTEXT_MEM_LOWER_BOUND) / sizeof(uint32_t);

  /* make sure all queue are not stopped/used */
  memset(trans_pcie->queue_stopped, 0, sizeof(trans_pcie->queue_stopped));
  memset(trans_pcie->queue_used, 0, sizeof(trans_pcie->queue_used));

  trans_pcie->scd_base_addr = iwl_read_prph(trans, SCD_SRAM_BASE_ADDR);

  WARN_ON(scd_base_addr != 0 && scd_base_addr != trans_pcie->scd_base_addr);

  /* reset context data, TX status and translation data */
  iwl_trans_write_mem(trans, trans_pcie->scd_base_addr + SCD_CONTEXT_MEM_LOWER_BOUND, NULL,
                      clear_dwords);

  iwl_write_prph(trans, SCD_DRAM_BASE_ADDR, trans_pcie->scd_bc_tbls.dma >> 10);

  /* The chain extension of the SCD doesn't work well. This feature is
   * enabled by default by the HW, so we need to disable it manually.
   */
  if (trans->cfg->base_params->scd_chain_ext_wa) {
    iwl_write_prph(trans, SCD_CHAINEXT_EN, 0);
  }

  iwl_trans_ac_txq_enable(trans, trans_pcie->cmd_queue, trans_pcie->cmd_fifo,
                          trans_pcie->cmd_q_wdg_timeout);

  /* Activate all Tx DMA/FIFO channels */
  iwl_scd_activate_fifos(trans);

  /* Enable DMA channel */
  for (chan = 0; chan < FH_TCSR_CHNL_NUM; chan++)
    iwl_write_direct32(
        trans, FH_TCSR_CHNL_TX_CONFIG_REG(chan),
        FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE | FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

  /* Update FH chicken bits */
  reg_val = iwl_read_direct32(trans, FH_TX_CHICKEN_BITS_REG);
  iwl_write_direct32(trans, FH_TX_CHICKEN_BITS_REG, reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

  /* Enable L1-Active */
  if (trans->cfg->device_family < IWL_DEVICE_FAMILY_8000) {
    iwl_clear_bits_prph(trans, APMG_PCIDEV_STT_REG, APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
  }
}

#if 0  // NEEDS_PORTING
void iwl_trans_pcie_tx_reset(struct iwl_trans* trans) {
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    int txq_id;

    /*
     * we should never get here in gen2 trans mode return early to avoid
     * having invalid accesses
     */
    if (WARN_ON_ONCE(trans->cfg->gen2)) { return; }

    for (txq_id = 0; txq_id < trans->cfg->base_params->num_of_queues; txq_id++) {
        struct iwl_txq* txq = trans_pcie->txq[txq_id];
        if (trans->cfg->use_tfh) {
            iwl_write_direct64(trans, FH_MEM_CBBC_QUEUE(trans, txq_id), txq->dma_addr);
        } else {
            iwl_write_direct32(trans, FH_MEM_CBBC_QUEUE(trans, txq_id), txq->dma_addr >> 8);
        }
        iwl_pcie_txq_unmap(trans, txq_id);
        txq->read_ptr = 0;
        txq->write_ptr = 0;
    }

    /* Tell NIC where to find the "keep warm" buffer */
    iwl_write_direct32(trans, FH_KW_MEM_ADDR_REG, trans_pcie->kw.dma >> 4);

    /*
     * Send 0 as the scd_base_addr since the device may have be reset
     * while we were in WoWLAN in which case SCD_SRAM_BASE_ADDR will
     * contain garbage.
     */
    iwl_pcie_tx_start(trans, 0);
}
#endif  // NEEDS_PORTING

static void iwl_pcie_tx_stop_fh(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  unsigned long flags;
  int ch, ret;
  uint32_t mask = 0;

  mtx_lock(&trans_pcie->irq_lock);

  if (!iwl_trans_grab_nic_access(trans, &flags)) {
    goto out;
  }

  /* Stop each Tx DMA channel */
  for (ch = 0; ch < FH_TCSR_CHNL_NUM; ch++) {
    iwl_write32(trans, FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
    mask |= FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch);
  }

  /* Wait for DMA channels to be idle */
  ret = iwl_poll_bit(trans, FH_TSSR_TX_STATUS_REG, mask, mask, 5000, NULL);
  if (ret != ZX_OK) {
    IWL_ERR(trans, "Failing on timeout while stopping DMA channel %d [0x%08x]\n", ch,
            iwl_read32(trans, FH_TSSR_TX_STATUS_REG));
  }

  iwl_trans_release_nic_access(trans, &flags);

out:
  mtx_unlock(&trans_pcie->irq_lock);
}

/*
 * iwl_pcie_tx_stop - Stop all Tx DMA channels
 */
zx_status_t iwl_pcie_tx_stop(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int txq_id;

  /* Turn off all Tx DMA fifos */
  iwl_scd_deactivate_fifos(trans);

  /* Turn off all Tx DMA channels */
  iwl_pcie_tx_stop_fh(trans);

  /*
   * This function can be called before the op_mode disabled the
   * queues. This happens when we have an rfkill interrupt.
   * Since we stop Tx altogether - mark the queues as stopped.
   */
  memset(trans_pcie->queue_stopped, 0, sizeof(trans_pcie->queue_stopped));
  memset(trans_pcie->queue_used, 0, sizeof(trans_pcie->queue_used));

  /* This can happen: start_hw, stop_device */
  if (!trans_pcie->txq_memory) {
    return ZX_OK;
  }

  /* Unmap DMA from host system and free skb's */
  for (txq_id = 0; txq_id < trans->cfg->base_params->num_of_queues; txq_id++) {
    iwl_pcie_txq_unmap(trans, txq_id);
  }

  return ZX_OK;
}

/*
 * iwl_trans_tx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void iwl_pcie_tx_free(struct iwl_trans* trans) {
  int txq_id;
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  memset(trans_pcie->queue_used, 0, sizeof(trans_pcie->queue_used));

  /* Tx queues */
  if (trans_pcie->txq_memory) {
    for (txq_id = 0; txq_id < trans->cfg->base_params->num_of_queues; txq_id++) {
      iwl_pcie_txq_free(trans, txq_id);
      trans_pcie->txq[txq_id] = NULL;
    }
  }

  free(trans_pcie->txq_memory);
  trans_pcie->txq_memory = NULL;

  iwl_pcie_free_dma_ptr(trans, &trans_pcie->kw);

  iwl_pcie_free_dma_ptr(trans, &trans_pcie->scd_bc_tbls);
}

/*
 * iwl_pcie_tx_alloc - allocate TX context
 * Allocate all Tx DMA structures and initialize them
 */
static zx_status_t iwl_pcie_tx_alloc(struct iwl_trans* trans) {
  zx_status_t ret;
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint16_t bc_tbls_size = trans->cfg->base_params->num_of_queues;

  bc_tbls_size *= (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560)
                      ? sizeof(struct iwl_gen3_bc_tbl)
                      : sizeof(struct iwlagn_scd_bc_tbl);

  /*It is not allowed to alloc twice, so warn when this happens.
   * We cannot rely on the previous allocation, so free and fail */
  if (WARN_ON(trans_pcie->txq_memory)) {
    ret = ZX_ERR_BAD_STATE;
    goto error;
  }

  ret = iwl_pcie_alloc_dma_ptr(trans, &trans_pcie->scd_bc_tbls, bc_tbls_size);
  if (ret != ZX_OK) {
    IWL_ERR(trans, "Scheduler BC Table allocation failed\n");
    goto error;
  }

  /* Alloc keep-warm buffer */
  ret = iwl_pcie_alloc_dma_ptr(trans, &trans_pcie->kw, IWL_KW_SIZE);
  if (ret != ZX_OK) {
    IWL_ERR(trans, "Keep Warm allocation failed\n");
    goto error;
  }

  trans_pcie->txq_memory = calloc(trans->cfg->base_params->num_of_queues, sizeof(struct iwl_txq));
  if (!trans_pcie->txq_memory) {
    IWL_ERR(trans, "Not enough memory for txq\n");
    ret = ZX_ERR_NO_MEMORY;
    goto error;
  }

  /* Alloc and init all Tx queues, including the command queue (#4/#9) */
  for (int txq_id = 0; txq_id < trans->cfg->base_params->num_of_queues; txq_id++) {
    bool cmd_queue = (txq_id == trans_pcie->cmd_queue);

    int slots_num = cmd_queue ? TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
    trans_pcie->txq[txq_id] = &trans_pcie->txq_memory[txq_id];
    ret = iwl_pcie_txq_alloc(trans, trans_pcie->txq[txq_id], slots_num, cmd_queue);
    if (ret != ZX_OK) {
      IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
      goto error;
    }
    trans_pcie->txq[txq_id]->id = txq_id;
  }

  return ZX_OK;

error:
  iwl_pcie_tx_free(trans);
  return ret;
}

zx_status_t iwl_pcie_tx_init(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int ret;
  bool alloc = false;

  if (!trans_pcie->txq_memory) {
    ret = iwl_pcie_tx_alloc(trans);
    if (ret) {
      goto error;
    }
    alloc = true;
  }

  mtx_lock(&trans_pcie->irq_lock);

  /* Turn off all Tx DMA fifos */
  iwl_scd_deactivate_fifos(trans);

  /* Tell NIC where to find the "keep warm" buffer */
  iwl_write_direct32(trans, FH_KW_MEM_ADDR_REG, trans_pcie->kw.dma >> 4);

  mtx_unlock(&trans_pcie->irq_lock);

  /* Alloc and init all Tx queues, including the command queue (#4/#9) */
  for (int txq_id = 0; txq_id < trans->cfg->base_params->num_of_queues; txq_id++) {
    bool cmd_queue = (txq_id == trans_pcie->cmd_queue);

    int slots_num = cmd_queue ? TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
    ret = iwl_pcie_txq_init(trans, trans_pcie->txq[txq_id], slots_num, cmd_queue);
    if (ret != ZX_OK) {
      IWL_ERR(trans, "Tx %d queue init failed\n", txq_id);
      goto error;
    }

    /*
     * Tell nic where to find circular buffer of TFDs for a
     * given Tx queue, and enable the DMA channel used for that
     * queue.
     * Circular buffer (TFD queue in DRAM) physical base address
     */
    iwl_write_direct32(trans, FH_MEM_CBBC_QUEUE(trans, txq_id),
                       trans_pcie->txq[txq_id]->dma_addr >> 8);
  }

  iwl_set_bits_prph(trans, SCD_GP_CTRL, SCD_GP_CTRL_AUTO_ACTIVE_MODE);
  if (trans->cfg->base_params->num_of_queues > 20) {
    iwl_set_bits_prph(trans, SCD_GP_CTRL, SCD_GP_CTRL_ENABLE_31_QUEUES);
  }

  return ZX_OK;
error:
  /*Upon error, free only if we allocated something */
  if (alloc) {
    iwl_pcie_tx_free(trans);
  }
  return ret;
}

static inline void iwl_pcie_txq_progress(struct iwl_txq* txq) {
  iwl_assert_lock_held(&txq->lock);

  if (!txq->wd_timeout) {
    return;
  }

  /*
   * station is asleep and we send data - that must
   * be uAPSD or PS-Poll. Don't rearm the timer.
   */
  if (txq->frozen) {
    return;
  }

  /*
   * if empty delete timer, otherwise move timer forward
   * since we're making progress on this queue
   */
  if (txq->read_ptr == txq->write_ptr) {
    iwlwifi_timer_stop(&txq->stuck_timer);
  } else {
    iwlwifi_timer_set(&txq->stuck_timer, txq->wd_timeout);
  }
}

/* Frees buffers until index _not_ inclusive */
void iwl_trans_pcie_reclaim(struct iwl_trans* trans, int txq_id, int ssn) {
#if 0  // NEEDS_PORTING
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct iwl_txq* txq = trans_pcie->txq[txq_id];
    int tfd_num = iwl_pcie_get_cmd_index(txq, ssn);
    int read_ptr = iwl_pcie_get_cmd_index(txq, txq->read_ptr);
    int last_to_free;

    /* This function is not meant to release cmd queue*/
    if (WARN_ON(txq_id == trans_pcie->cmd_queue)) { return; }

    spin_lock_bh(&txq->lock);

    if (!test_bit(txq_id, trans_pcie->queue_used)) {
        IWL_DEBUG_TX_QUEUES(trans, "Q %d inactive - ignoring idx %d\n", txq_id, ssn);
        goto out;
    }

    if (read_ptr == tfd_num) { goto out; }

    IWL_DEBUG_TX_REPLY(trans, "[Q %d] %d -> %d (%d)\n", txq_id, txq->read_ptr, tfd_num, ssn);

    /*Since we free until index _not_ inclusive, the one before index is
     * the last we will free. This one must be used */
    last_to_free = iwl_queue_dec_wrap(trans, tfd_num);

    if (!iwl_queue_used(txq, last_to_free)) {
        IWL_ERR(trans,
                "%s: Read index for txq id (%d), last_to_free %d is out of range [0-%d] %d %d.\n",
                __func__, txq_id, last_to_free, trans->cfg->base_params->max_tfd_queue_size,
                txq->write_ptr, txq->read_ptr);
        goto out;
    }

    if (WARN_ON(!skb_queue_empty(skbs))) { goto out; }

    for (; read_ptr != tfd_num; txq->read_ptr = iwl_queue_inc_wrap(trans, txq->read_ptr),
                                read_ptr = iwl_pcie_get_cmd_index(txq, txq->read_ptr)) {
        struct sk_buff* skb = txq->entries[read_ptr].skb;

        if (WARN_ON_ONCE(!skb)) { continue; }

        iwl_pcie_free_tso_page(trans_pcie, skb);

        __skb_queue_tail(skbs, skb);

        txq->entries[read_ptr].skb = NULL;

        if (!trans->cfg->use_tfh) { iwl_pcie_txq_inval_byte_cnt_tbl(trans, txq); }

        iwl_pcie_txq_free_tfd(trans, txq);
    }

    iwl_pcie_txq_progress(txq);

    if (iwl_queue_space(trans, txq) > txq->low_mark &&
        test_bit(txq_id, trans_pcie->queue_stopped)) {
        struct sk_buff_head overflow_skbs;

        __skb_queue_head_init(&overflow_skbs);
        skb_queue_splice_init(&txq->overflow_q, &overflow_skbs);

        /*
         * This is tricky: we are in reclaim path which is non
         * re-entrant, so noone will try to take the access the
         * txq data from that path. We stopped tx, so we can't
         * have tx as well. Bottom line, we can unlock and re-lock
         * later.
         */
        spin_unlock_bh(&txq->lock);

        while (!skb_queue_empty(&overflow_skbs)) {
            struct sk_buff* skb = __skb_dequeue(&overflow_skbs);
            struct iwl_device_cmd* dev_cmd_ptr;

            dev_cmd_ptr = *(void**)((uint8_t*)skb->cb + trans_pcie->dev_cmd_offs);

            /*
             * Note that we can very well be overflowing again.
             * In that case, iwl_queue_space will be small again
             * and we won't wake mac80211's queue.
             */
            iwl_trans_tx(trans, skb, dev_cmd_ptr, txq_id);
        }

        if (iwl_queue_space(trans, txq) > txq->low_mark) { iwl_wake_queue(trans, txq); }

        spin_lock_bh(&txq->lock);
    }

    if (txq->read_ptr == txq->write_ptr) {
        IWL_DEBUG_RPM(trans, "Q %d - last tx reclaimed\n", txq->id);
        iwl_trans_unref(trans);
    }

out:
    spin_unlock_bh(&txq->lock);
#endif  // NEEDS_PORTING
  IWL_ERR(trans, "%s needs porting\n", __FUNCTION__);
}

static zx_status_t iwl_pcie_set_cmd_in_flight(struct iwl_trans* trans,
                                              const struct iwl_host_cmd* cmd) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  const struct iwl_cfg* cfg = trans->cfg;

  iwl_assert_lock_held(&trans_pcie->reg_lock);

  /* Make sure the NIC is still alive in the bus */
  if (test_bit(STATUS_TRANS_DEAD, &trans->status)) {
    return ZX_ERR_BAD_STATE;
  }

  if (!(cmd->flags & CMD_SEND_IN_IDLE) && !trans_pcie->ref_cmd_in_flight) {
    trans_pcie->ref_cmd_in_flight = true;
    IWL_DEBUG_RPM(trans, "set ref_cmd_in_flight - ref\n");
    iwl_trans_ref(trans);
  }

  /*
   * wake up the NIC to make sure that the firmware will see the host
   * command - we will let the NIC sleep once all the host commands
   * returned. This needs to be done only on NICs that have
   * apmg_wake_up_wa set.
   */
  if (cfg->base_params->apmg_wake_up_wa && !trans_pcie->cmd_hold_nic_awake) {
    __iwl_trans_pcie_set_bit(trans, CSR_GP_CNTRL, BIT(cfg->csr->flag_mac_access_req));

    zx_status_t status = iwl_poll_bit(
        trans, CSR_GP_CNTRL, BIT(cfg->csr->flag_val_mac_access_en),
        (BIT(cfg->csr->flag_mac_clock_ready) | CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000, NULL);
    if (status != ZX_OK) {
      __iwl_trans_pcie_clear_bit(trans, CSR_GP_CNTRL, BIT(cfg->csr->flag_mac_access_req));
      IWL_ERR(trans, "Failed to wake NIC for hcmd\n");
      return ZX_ERR_IO;
    }
    trans_pcie->cmd_hold_nic_awake = true;
  }

  return ZX_OK;
}

/*
 * iwl_pcie_cmdq_reclaim - Reclaim TX command queue entries already Tx'd
 *
 * When FW advances 'R' index, all entries between old and new 'R' index
 * need to be reclaimed. As result, some free space forms.  If there is
 * enough free space (> low mark), wake the stack that feeds us.
 */
zx_status_t iwl_pcie_cmdq_reclaim(struct iwl_trans* trans, int txq_id, uint32_t idx) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  int nfreed = 0;

  // Ensure the tx_id is pointing to the cmd_queue.
  if (txq_id != trans_pcie->cmd_queue) {
    IWL_WARN(trans, "wrong command queue %d (should be %d)\n", txq_id, trans_pcie->cmd_queue);
    return ZX_ERR_INVALID_ARGS;
  }

  iwl_assert_lock_held(&txq->lock);

  idx = iwl_pcie_get_cmd_index(txq, idx);
  uint16_t r = iwl_pcie_get_cmd_index(txq, txq->read_ptr);

  if (idx >= trans->cfg->base_params->max_tfd_queue_size || (!iwl_queue_used(txq, idx))) {
    if (test_bit(txq_id, trans_pcie->queue_used)) {
      IWL_WARN(trans, "DMA queue txq_id (%d), read index %d is out of range [0-%d] wp:%d rp:%d\n",
               txq_id, idx, trans->cfg->base_params->max_tfd_queue_size, txq->write_ptr,
               txq->read_ptr);
    }
    return ZX_ERR_OUT_OF_RANGE;
  }

  for (idx = iwl_queue_inc_wrap(trans, idx); r != idx; r = iwl_queue_inc_wrap(trans, r)) {
    txq->read_ptr = iwl_queue_inc_wrap(trans, txq->read_ptr);

    if (nfreed++ > 0) {
      IWL_ERR(trans, "HCMD skipped: index (%d) %d %d\n", idx, txq->write_ptr, r);
      iwl_force_nmi(trans);
      return ZX_ERR_BAD_STATE;
    }
  }

  if (txq->read_ptr == txq->write_ptr) {
    mtx_lock(&trans_pcie->reg_lock);
    iwl_pcie_clear_cmd_in_flight(trans);
    mtx_unlock(&trans_pcie->reg_lock);
  }

  iwl_pcie_txq_progress(txq);
  return ZX_OK;
}

static int iwl_pcie_txq_set_ratid_map(struct iwl_trans* trans, uint16_t ra_tid, uint16_t txq_id) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t tbl_dw_addr;
  uint32_t tbl_dw;
  uint16_t scd_q2ratid;

  scd_q2ratid = ra_tid & SCD_QUEUE_RA_TID_MAP_RATID_MSK;

  tbl_dw_addr = trans_pcie->scd_base_addr + SCD_TRANS_TBL_OFFSET_QUEUE(txq_id);

  tbl_dw = iwl_trans_read_mem32(trans, tbl_dw_addr);

  if (txq_id & 0x1) {
    tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
  } else {
    tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);
  }

  iwl_trans_write_mem32(trans, tbl_dw_addr, tbl_dw);

  return 0;
}

/* Receiver address (actually, Rx station's index into station table),
 * combined with Traffic ID (QOS priority), in format used by Tx Scheduler */
#define BUILD_RAxTID(sta_id, tid) (((sta_id) << 4) + (tid))

bool iwl_trans_pcie_txq_enable(struct iwl_trans* trans, int txq_id, uint16_t ssn,
                               const struct iwl_trans_txq_scd_cfg* cfg, zx_duration_t wdg_timeout) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  int fifo = -1;
  bool scd_bug = false;

  if (test_and_set_bit(txq_id, trans_pcie->queue_used)) {
    IWL_WARN(trans, "queue %d already used - expect issues", txq_id);
  }

  txq->wd_timeout = wdg_timeout;

  if (cfg) {
    fifo = cfg->fifo;

    /* Disable the scheduler prior configuring the cmd queue */
    if (txq_id == trans_pcie->cmd_queue && trans_pcie->scd_set_active) {
      iwl_scd_enable_set_active(trans, 0);
    }

    /* Stop this Tx queue before configuring it */
    iwl_scd_txq_set_inactive(trans, txq_id);

    /* Set this queue as a chain-building queue unless it is CMD */
    if (txq_id != trans_pcie->cmd_queue) {
      iwl_scd_txq_set_chain(trans, txq_id);
    }

    if (cfg->aggregate) {
      uint16_t ra_tid = BUILD_RAxTID(cfg->sta_id, cfg->tid);

      /* Map receiver-address / traffic-ID to this queue */
      iwl_pcie_txq_set_ratid_map(trans, ra_tid, txq_id);

      /* enable aggregations for the queue */
      iwl_scd_txq_enable_agg(trans, txq_id);
      txq->ampdu = true;
    } else {
      /*
       * disable aggregations for the queue, this will also
       * make the ra_tid mapping configuration irrelevant
       * since it is now a non-AGG queue.
       */
      iwl_scd_txq_disable_agg(trans, txq_id);

      ssn = txq->read_ptr;
    }
  } else {
    /*
     * If we need to move the SCD write pointer by steps of
     * 0x40, 0x80 or 0xc0, it gets stuck. Avoids this and let
     * the op_mode know by returning true later.
     * Do this only in case cfg is NULL since this trick can
     * be done only if we have DQA enabled which is true for mvm
     * only. And mvm never sets a cfg pointer.
     * This is really ugly, but this is the easiest way out for
     * this sad hardware issue.
     * This bug has been fixed on devices 9000 and up.
     */
    scd_bug =
        !trans->cfg->mq_rx_supported && !((ssn - txq->write_ptr) & 0x3f) && (ssn != txq->write_ptr);
    if (scd_bug) {
      ssn++;
    }
  }

  /* Place first TFD at index corresponding to start sequence number.
   * Assumes that ssn_idx is valid (!= 0xFFF) */
  txq->read_ptr = (ssn & 0xff);
  txq->write_ptr = (ssn & 0xff);
  iwl_write_direct32(trans, HBUS_TARG_WRPTR, (ssn & 0xff) | (txq_id << 8));

  if (cfg) {
    uint8_t frame_limit = cfg->frame_limit;

    iwl_write_prph(trans, SCD_QUEUE_RDPTR(txq_id), ssn);

    /* Set up Tx window size and frame limit for this queue */
    iwl_trans_write_mem32(trans, trans_pcie->scd_base_addr + SCD_CONTEXT_QUEUE_OFFSET(txq_id), 0);
    iwl_trans_write_mem32(
        trans, trans_pcie->scd_base_addr + SCD_CONTEXT_QUEUE_OFFSET(txq_id) + sizeof(uint32_t),
        SCD_QUEUE_CTX_REG2_VAL(WIN_SIZE, frame_limit) |
            SCD_QUEUE_CTX_REG2_VAL(FRAME_LIMIT, frame_limit));

    /* Set up status area in SRAM, map to Tx DMA/FIFO, activate */
    iwl_write_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id),
                   (1 << SCD_QUEUE_STTS_REG_POS_ACTIVE) |
                       (cfg->fifo << SCD_QUEUE_STTS_REG_POS_TXF) |
                       (1 << SCD_QUEUE_STTS_REG_POS_WSL) | SCD_QUEUE_STTS_REG_MSK);

    /* enable the scheduler for this queue (only) */
    if (txq_id == trans_pcie->cmd_queue && trans_pcie->scd_set_active) {
      iwl_scd_enable_set_active(trans, BIT(txq_id));
    }

    IWL_DEBUG_TX_QUEUES(trans, "Activate queue %d on FIFO %d WrPtr: %d\n", txq_id, fifo,
                        ssn & 0xff);
  } else {
    IWL_DEBUG_TX_QUEUES(trans, "Activate queue %d WrPtr: %d\n", txq_id, ssn & 0xff);
  }

  return scd_bug;
}

void iwl_trans_pcie_txq_set_shared_mode(struct iwl_trans* trans, uint32_t txq_id,
                                        bool shared_mode) {
#if 0  // NEEDS_PORTING
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct iwl_txq* txq = trans_pcie->txq[txq_id];

    txq->ampdu = !shared_mode;
#endif  // NEEDS_PORTING
  IWL_ERR(trans, "%s needs porting\n", __FUNCTION__);
}

void iwl_trans_pcie_txq_disable(struct iwl_trans* trans, int txq_id, bool configure_scd) {
#if 0  // NEEDS_PORTING
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    uint32_t stts_addr = trans_pcie->scd_base_addr + SCD_TX_STTS_QUEUE_OFFSET(txq_id);
    static const uint32_t zero_val[4] = {};

    trans_pcie->txq[txq_id]->frozen_expiry_remainder = 0;
    trans_pcie->txq[txq_id]->frozen = false;

    /*
     * Upon HW Rfkill - we stop the device, and then stop the queues
     * in the op_mode. Just for the sake of the simplicity of the op_mode,
     * allow the op_mode to call txq_disable after it already called
     * stop_device.
     */
    if (!test_and_clear_bit(txq_id, trans_pcie->queue_used)) {
        WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status), "queue %d not used", txq_id);
        return;
    }

    if (configure_scd) {
        iwl_scd_txq_set_inactive(trans, txq_id);

        iwl_trans_write_mem(trans, stts_addr, (void*)zero_val, ARRAY_SIZE(zero_val));
    }

    iwl_pcie_txq_unmap(trans, txq_id);
    trans_pcie->txq[txq_id]->ampdu = false;

    IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", txq_id);
#endif  // NEEDS_PORTING
  IWL_ERR(trans, "%s needs porting\n", __FUNCTION__);
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

//
// iwl_pcie_enqueue_hcmd - enqueue a uCode command
//
// @priv: device private data point
// @cmd: a pointer to the ucode command structure
//
// Below is how a Tx host command is built:
//
// A host command from MVM can contain 2 data framgents:
//
//   - The first fragment must contain the command header including command ID, length ... etc.
//     It may not contain the payload since it can be passed in the second fragment.
//
//   - The second fragment is optional. Usually it is used for different flags from the first
//     fragment. For example, copying a host command with very large payload has performance
//     concern. Then the first fragment can only contain the command header while the second
//     fragment can contain its large payload with NOCOPY flag.
//
// So, down to the transport layer, a host command can be re-mapped to multiple descriptors in order
// to satisfy the upper layer's demand. This is why TFD (Transmit Frame Descriptor) is introduced.
//
// txq->tfds[] (TFDs) are used by driver to indicate the data fragements for firmware. A host
// command can be re-mapped into 1~3 descriptors depending on the fragment lengths and flags
// (check how the iwl_pcie_txq_build_tfd() is called in this function):
//
//   - For small-sized command (<= 20 bytes):
//
//     At driver initialization time, it already allocated a special buffer (txq->first_tb_bufs[],
//     also called 'tb0' in this function) for small host commands.
//
//     For these commands, no memory mapping is required, and just copy the whole command to the
//     buffer. Note that each entry in this buffer must be 64-byte aligned although only the first
//     20-byte is used.
//
//        <-- 20-B -->
//       +------------+
//       |     tb0    |
//       +------------+
//
//
//   - For medium-sized command (20 < len <= 328 bytes):
//
//     The first 20-byte still goes to 'tb0'. The remaining content will be mapped into the second
//     descriptor -- the 'cmd' io_buffer in 'struct iwl_pcie_txq_entry'.
//
//        <-----------   20 ~ 328 bytes   ----------->
//       +------------+  +----------------------------+
//       |     tb0    |  |   2nd descriptor ('cmd')   |
//       +------------+  +----------------------------+
//           1st fragment (or with the 2nd fragment)
//
//
//   - For large-sized command (> 328 bytes):
//
//     It cannot be fit within one fragment (seems a hardware issue?). The second fragment must be
//     marked with NOCOPY flag (observed from the code using this function).
//
//     + If first fragment is smaller than or equal to 20-byte, then 2 descriptors will be built.
//       The first descriptor points to the first fragment while the second descriptor points to
//        second fragment -- the 'dup_io_buf' in 'struct iwl_pcie_txq_entry'.
//
//          <-- 20-B -->    <---------  any length  ---------->
//         +------------+  +-----------------------------------+
//         |     tb0    |  |   2nd descriptor ('dup_io_buf')   |
//         +------------+  +-----------------------------------+
//          1st fragment       2nd fragment (NOCOPY)
//
//
//     + If the first fragment is larger than 20-byte, similar as the medium-sized command, the
//       first fragment will be split into 2 descriptos: 'tb0' and 'cmd' io_buffer. However, the
//       second fragment (with the NOCOPY flag) will be stored in 3rd descriptor.
//
//          <-----------   20 ~ 328 bytes   ----------->    <---------  any length  ---------->
//         +------------+  +----------------------------+  +-----------------------------------+
//         |     tb0    |  |   2nd descriptor ('cmd')   |  |   3rd descriptor ('dup_io_buf')   |
//         +------------+  +----------------------------+  +-----------------------------------+
//                     1st fragment                              2nd fragment (NOCOPY)
//
static zx_status_t iwl_pcie_enqueue_hcmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd,
                                         int* cmd_idx_out) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];
  struct iwl_cmd_meta* out_meta;
  bool had_dup_flag = false;
  uint16_t copy_size;  // The size to copy into allocated DMA area (without the NOCOPY data).
  uint16_t cmd_size;   // Whole command size writing to HW, including header and data.
  bool had_nocopy = false;
  uint8_t group_id = iwl_cmd_groupid(cmd->id);
  const uint8_t* cmddata[IWL_MAX_CMD_TBS_PER_TFD];
  uint16_t cmdlen[IWL_MAX_CMD_TBS_PER_TFD];  // Locally manipulated data lengths.
  zx_status_t status = ZX_OK;

  if (!trans->wide_cmd_header && group_id > IWL_ALWAYS_LONG_GROUP) {
    IWL_WARN(trans, "unsupported wide command %#x\n", cmd->id);
    return ZX_ERR_INVALID_ARGS;
  }

  if (group_id != 0) {
    copy_size = sizeof(struct iwl_cmd_header_wide);
    cmd_size = sizeof(struct iwl_cmd_header_wide);
  } else {
    copy_size = sizeof(struct iwl_cmd_header);
    cmd_size = sizeof(struct iwl_cmd_header);
  }

  /* need one for the header if the first is NOCOPY */
  BUILD_BUG_ON(IWL_MAX_CMD_TBS_PER_TFD > IWL_NUM_OF_TBS - 1);

  for (int i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
    cmddata[i] = cmd->data[i];
    cmdlen[i] = cmd->len[i];

    if (!cmd->len[i]) {
      continue;
    }

    /* need at least IWL_FIRST_TB_SIZE copied */
    if (copy_size < IWL_FIRST_TB_SIZE) {
      int copy = IWL_FIRST_TB_SIZE - copy_size;

      if (copy > cmdlen[i]) {
        copy = cmdlen[i];
      }
      cmdlen[i] -= copy;
      cmddata[i] += copy;
      copy_size += copy;
    }

    if (cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY) {
      had_nocopy = true;
      if (WARN_ON(cmd->dataflags[i] & IWL_HCMD_DFL_DUP)) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else if (cmd->dataflags[i] & IWL_HCMD_DFL_DUP) {
      /*
       * This is also a chunk that isn't copied
       * to the static buffer so set had_nocopy.
       */
      had_nocopy = true;

      /* only allowed once */
      if (WARN_ON(had_dup_flag)) {
        return ZX_ERR_INVALID_ARGS;
      }

      had_dup_flag = true;
    } else {
      /* NOCOPY must not be followed by normal! */
      if (WARN_ON(had_nocopy)) {
        return ZX_ERR_INVALID_ARGS;
      }
      copy_size += cmdlen[i];
    }
    cmd_size += cmd->len[i];
  }

  /*
   * If any of the command structures end up being larger than
   * the TFD_MAX_PAYLOAD_SIZE and they aren't dynamically
   * allocated into separate TFDs, then we will need to
   * increase the size of the buffers.
   */
  if (copy_size > TFD_MAX_PAYLOAD_SIZE) {
    IWL_WARN(trans, "Command %s (%#x) is too large (%d bytes, expect <= %lu bytes)\n",
             iwl_get_cmd_string(trans, cmd->id), cmd->id, copy_size, TFD_MAX_PAYLOAD_SIZE);
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&txq->lock);

  if (iwl_queue_space(trans, txq) < ((cmd->flags & CMD_ASYNC) ? 2 : 1)) {
    mtx_unlock(&txq->lock);
    IWL_ERR(trans, "No space in command queue\n");
    iwl_op_mode_cmd_queue_full(trans->op_mode);
    return ZX_ERR_NO_RESOURCES;
  }

  int cmd_idx = iwl_pcie_get_cmd_index(txq, txq->write_ptr);
  struct iwl_device_cmd* out_cmd = io_buffer_virt(&txq->entries[cmd_idx].cmd);
  zx_paddr_t phys_addr = io_buffer_phys(&txq->entries[cmd_idx].cmd);
  out_meta = &txq->entries[cmd_idx].meta;

  memset(out_meta, 0, sizeof(*out_meta)); /* re-initialize to NULL */
  if (cmd->flags & CMD_WANT_SKB) {
    out_meta->source = cmd;
  }

  /* set up the header */
  uint32_t cmd_pos;  // Pointer used with 'out_cmd' to indicate the location for 'next copy data'.
  if (group_id != 0) {
    out_cmd->hdr_wide.cmd = iwl_cmd_opcode(cmd->id);
    out_cmd->hdr_wide.group_id = group_id;
    out_cmd->hdr_wide.version = iwl_cmd_version(cmd->id);
    out_cmd->hdr_wide.length = cpu_to_le16(cmd_size - sizeof(struct iwl_cmd_header_wide));
    out_cmd->hdr_wide.reserved = 0;
    out_cmd->hdr_wide.sequence =
        cpu_to_le16(QUEUE_TO_SEQ(trans_pcie->cmd_queue) | INDEX_TO_SEQ(txq->write_ptr));

    cmd_pos = sizeof(struct iwl_cmd_header_wide);
    copy_size = sizeof(struct iwl_cmd_header_wide);
  } else {
    out_cmd->hdr.cmd = iwl_cmd_opcode(cmd->id);
    out_cmd->hdr.sequence =
        cpu_to_le16(QUEUE_TO_SEQ(trans_pcie->cmd_queue) | INDEX_TO_SEQ(txq->write_ptr));
    out_cmd->hdr.group_id = 0;

    cmd_pos = sizeof(struct iwl_cmd_header);
    copy_size = sizeof(struct iwl_cmd_header);
  }

  /* and copy the data that needs to be copied */
  for (int i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
    int copy;

    if (!cmd->len[i]) {
      continue;
    }

    /* copy everything if not nocopy/dup */
    if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY | IWL_HCMD_DFL_DUP))) {
      copy = cmd->len[i];

      memcpy((uint8_t*)out_cmd + cmd_pos, cmd->data[i], copy);
      cmd_pos += copy;
      copy_size += copy;
      continue;
    }

    /*
     * Otherwise we need at least IWL_FIRST_TB_SIZE copied
     * in total (for bi-directional DMA), but copy up to what
     * we can fit into the payload for debug dump purposes.
     * TODO(43084): Remove the un-necessary memcpy below.
     */
    copy = min_t(int, TFD_MAX_PAYLOAD_SIZE - cmd_pos, cmd->len[i]);
    memcpy((uint8_t*)out_cmd + cmd_pos, cmd->data[i], copy);
    cmd_pos += copy;

    /* However, treat copy_size the proper way, we need it below */
    if (copy_size < IWL_FIRST_TB_SIZE) {
      copy = IWL_FIRST_TB_SIZE - copy_size;

      if (copy > cmd->len[i]) {
        copy = cmd->len[i];
      }
      copy_size += copy;
    }
  }

  IWL_DEBUG_HC(trans, "Sending command %s (%.2x.%.2x), seq: 0x%04X, %d bytes at %d[%d]:%d\n",
               iwl_get_cmd_string(trans, cmd->id), group_id, out_cmd->hdr.cmd,
               le16_to_cpu(out_cmd->hdr.sequence), cmd_size, txq->write_ptr, cmd_idx,
               trans_pcie->cmd_queue);

  // start the TFD with the minimum copy bytes (tb0).
  struct iwl_pcie_first_tb_buf* tb_bufs = io_buffer_virt(&txq->first_tb_bufs);
  uint16_t tb0_size = min_t(int, copy_size, IWL_FIRST_TB_SIZE);
  memcpy(&tb_bufs[cmd_idx], &out_cmd->hdr, tb0_size);
  uint32_t num_tbs;
  iwl_pcie_txq_build_tfd(trans, txq, iwl_pcie_get_first_tb_dma(txq, cmd_idx), tb0_size, true,
                         &num_tbs);

  /* map first command fragment, if any remains */
  if (copy_size > tb0_size) {
    iwl_pcie_txq_build_tfd(trans, txq, phys_addr + tb0_size, copy_size - tb0_size, false, &num_tbs);
  }

  /* map the remaining (adjusted) nocopy/dup fragments */
  bool used_dup_io_buf = false;
  for (int i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
    const void* data = cmddata[i];

    if (!cmdlen[i]) {
      continue;
    }
    if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY | IWL_HCMD_DFL_DUP))) {
      continue;
    }

    // Assume only one fragment needs DUP and NOCOPY. Needs to extend the txq_entry.dup_io_buf to 2
    // if we need to support 2 NOCOPY fragments.
    if (used_dup_io_buf) {
      mtx_unlock(&txq->lock);
      IWL_ERR(trans, "Cannot have 2 NOCOPY or DUP fragments in one command.\n");
      return ZX_ERR_IO_INVALID;
    } else {
      used_dup_io_buf = true;
    }

    // Allocate an io_buffer to store the remaining data (either a DUP or a NOCOPY fragment).
    //
    // For the DUP case, as the flag described, the data is copied into the io_buffer for the
    // caller to use it in Rx path.
    //
    // For the NOCOPY case, since it is larger than TFD_MAX_PAYLOAD_SIZE, we have to copy the data
    // into the io_buffer and map it to physical address. However, the original purpose of this flag
    // is to avoid copy due to performance consideration. So created TODO(42212) to track this.
    //
    io_buffer_t* dup_io_buf = &txq->entries[cmd_idx].dup_io_buf;

    // Allocate a cached io_buffer, copy the data, and flush the cache at once. The io_buffer will
    // be released (reclaimed) in iwl_pcie_rx_handle_rb().
    //
    // In theory, using cached io_buffer is faster. No matter how memcpy is implemented (copying
    // byte-by-byte or word-by-word), writing to cache always has smaller cycles than writing to
    // SDRAM. Even during the cache flush stage, the memory write is done in cache-line size, which
    // is still faster than CPU write.
    //
    // However, it is arguable weather flush is needed or not since some x86 platforms/PCIe devices
    // support cached read. But this is not guaranteed on all platforms (e.g. ARM). So let's play
    // safe first.
    //
    ZX_ASSERT(!io_buffer_is_valid(dup_io_buf));
    uint16_t dup_len = cmdlen[i];
    io_buffer_init(dup_io_buf, trans_pcie->bti, dup_len, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    void* virt_addr = io_buffer_virt(dup_io_buf);
    memcpy(virt_addr, data, dup_len);
    phys_addr = io_buffer_phys(dup_io_buf);
    iwl_pcie_txq_build_tfd(trans, txq, phys_addr, dup_len, false, &num_tbs);
    io_buffer_cache_flush(dup_io_buf, 0, dup_len);
  }

  BUILD_BUG_ON(IWL_TFH_NUM_TBS > sizeof(out_meta->tbs) * BITS_PER_BYTE);

  out_meta->flags = cmd->flags;

#if 0  // NEEDS_PORTING
  trace_iwlwifi_dev_hcmd(trans->dev, cmd, cmd_size, &out_cmd->hdr_wide);
#endif  // NEEDS_PORTING

  /* start timer if queue currently empty */
  if (txq->read_ptr == txq->write_ptr && txq->wd_timeout) {
    iwlwifi_timer_set(&txq->stuck_timer, txq->wd_timeout);
  }

  mtx_lock(&trans_pcie->reg_lock);
  status = iwl_pcie_set_cmd_in_flight(trans, cmd);
  if (status != ZX_OK) {
    mtx_unlock(&trans_pcie->reg_lock);
    mtx_unlock(&txq->lock);
    return status;
  }

  /* Increment and update queue's write index */
  txq->write_ptr = iwl_queue_inc_wrap(trans, txq->write_ptr);
  iwl_pcie_txq_inc_wr_ptr(trans, txq);
  mtx_unlock(&trans_pcie->reg_lock);
  mtx_unlock(&txq->lock);

  if (cmd_idx_out) {
    *cmd_idx_out = cmd_idx;
  }

  return ZX_OK;
}

/*
 * iwl_pcie_hcmd_complete - Pull unused buffers off the queue and reclaim them
 * @rxb: Rx buffer to reclaim
 */
void iwl_pcie_hcmd_complete(struct iwl_trans* trans, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  uint16_t sequence = le16_to_cpu(pkt->hdr.sequence);
  uint8_t group_id;
  uint32_t cmd_id;
  int txq_id = SEQ_TO_QUEUE(sequence);
  int index = SEQ_TO_INDEX(sequence);
  int cmd_index;
  struct iwl_device_cmd* cmd;
  struct iwl_cmd_meta* meta;
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];

  /* If a Tx command is being handled and it isn't in the actual
   * command queue then there a command routing bug has been introduced
   * in the queue management code. */
  if (txq_id != trans_pcie->cmd_queue) {
    IWL_WARN(trans, "wrong command queue %d (should be %d), sequence 0x%X readp=%d writep=%d\n",
             txq_id, trans_pcie->cmd_queue, sequence, txq->read_ptr, txq->write_ptr);
#if 0  // NEEDS_PORTING
      iwl_print_hex_error(trans, pkt, 32);
#endif  // NEEDS_PORTING
    return;
  }

  mtx_lock(&txq->lock);

  cmd_index = iwl_pcie_get_cmd_index(txq, index);
  cmd = (struct iwl_device_cmd*)io_buffer_virt(&txq->entries[cmd_index].cmd);
  meta = &txq->entries[cmd_index].meta;
  group_id = cmd->hdr.group_id;
  cmd_id = iwl_cmd_id(cmd->hdr.cmd, group_id, 0);

  iwl_pcie_tfd_unmap(trans, meta, txq, index);

  /* Input error checking is done when commands are added to queue. */
  if (meta->flags & CMD_WANT_SKB) {
#if 0  // NEEDS_PORTING
        struct page* p = rxb_steal_page(rxb);
#endif  // NEEDS_PORTING

    meta->source->resp_pkt = pkt;
#if 0  // NEEDS_PORTING
        meta->source->_rx_page_addr = (unsigned long)page_address(p);
        meta->source->_rx_page_order = trans_pcie->rx_page_order;
#endif  // NEEDS_PORTING
  }

  if (meta->flags & CMD_WANT_ASYNC_CALLBACK) {
    iwl_op_mode_async_cb(trans->op_mode, cmd);
  }

  iwl_pcie_cmdq_reclaim(trans, txq_id, index);

  if (!(meta->flags & CMD_ASYNC)) {
    if (!test_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status)) {
      IWL_WARN(trans, "HCMD_ACTIVE already clear for command %s\n",
               iwl_get_cmd_string(trans, cmd_id));
    }
    clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
    IWL_DEBUG_INFO(trans, "Clearing HCMD_ACTIVE for command %s\n",
                   iwl_get_cmd_string(trans, cmd_id));
    sync_completion_signal(&trans_pcie->wait_command_queue);
  }

  if (meta->flags & CMD_MAKE_TRANS_IDLE) {
    IWL_DEBUG_INFO(trans, "complete %s - mark trans as idle\n",
                   iwl_get_cmd_string(trans, cmd->hdr.cmd));
    set_bit(STATUS_TRANS_IDLE, &trans->status);
#if 0  // NEEDS_PORTING
        wake_up(&trans_pcie->d0i3_waitq);
#endif  // NEEDS_PORTING
  }

  if (meta->flags & CMD_WAKE_UP_TRANS) {
    IWL_DEBUG_INFO(trans, "complete %s - clear trans idle flag\n",
                   iwl_get_cmd_string(trans, cmd->hdr.cmd));
    clear_bit(STATUS_TRANS_IDLE, &trans->status);
#if 0  // NEEDS_PORTING
        wake_up(&trans_pcie->d0i3_waitq);
#endif  // NEEDS_PORTING
  }

  meta->flags = 0;

  mtx_unlock(&txq->lock);
}

// (2 * HZ * CPTCFG_IWL_TIMEOUT_FACTOR) where CPTCFG_IWL_TIMEOUT_FACTOR is 1 by default
#define HOST_COMPLETE_TIMEOUT ZX_SEC(2)

static zx_status_t iwl_pcie_send_hcmd_async(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  /* An asynchronous command can not expect an SKB to be set. */
  if (WARN_ON(cmd->flags & CMD_WANT_SKB)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = iwl_pcie_enqueue_hcmd(trans, cmd, /*cmd_idx=*/NULL);
  if (status != ZX_OK) {
    IWL_ERR(trans, "Error sending %s: enqueue_hcmd failed: %d\n",
            iwl_get_cmd_string(trans, cmd->id), status);
    return status;
  }
  return ZX_OK;
}

static zx_status_t iwl_pcie_send_hcmd_sync(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];

  IWL_DEBUG_INFO(trans, "Attempting to send sync command %s\n", iwl_get_cmd_string(trans, cmd->id));

  if (test_and_set_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status)) {
    IWL_WARN(trans, "Command %s: a command is already active!\n",
             iwl_get_cmd_string(trans, cmd->id));
    return ZX_ERR_IO;
  }

  IWL_DEBUG_INFO(trans, "Setting HCMD_ACTIVE for command %s\n", iwl_get_cmd_string(trans, cmd->id));

#if 0  // NEEDS_PORTING
    if (pm_runtime_suspended(&trans_pcie->pci_dev->dev)) {
        ret =
            wait_event_timeout(trans_pcie->d0i3_waitq, pm_runtime_active(&trans_pcie->pci_dev->dev),
                               msecs_to_jiffies(IWL_TRANS_IDLE_TIMEOUT));
        if (!ret) {
            IWL_ERR(trans, "Timeout exiting D0i3 before hcmd\n");
            return -ETIMEDOUT;
        }
    }
#endif  // NEEDS_PORTING

  sync_completion_reset(&trans_pcie->wait_command_queue);

  int cmd_idx;
  zx_status_t status = iwl_pcie_enqueue_hcmd(trans, cmd, &cmd_idx);
  if (status != ZX_OK) {
    clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
    IWL_ERR(trans, "Error sending %s: enqueue_hcmd failed: %d\n",
            iwl_get_cmd_string(trans, cmd->id), status);
    return status;
  }

  status = sync_completion_wait(&trans_pcie->wait_command_queue, HOST_COMPLETE_TIMEOUT);
  if (status != ZX_OK) {
    IWL_ERR(trans, "Error sending %s: time out after %ldms (%s).\n",
            iwl_get_cmd_string(trans, cmd->id),
            zx_nsec_from_duration(HOST_COMPLETE_TIMEOUT) / 1000000, zx_status_get_string(status));

    IWL_ERR(trans, "Current CMD queue read_ptr %d write_ptr %d\n", txq->read_ptr, txq->write_ptr);

    clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
    IWL_DEBUG_INFO(trans, "Clearing HCMD_ACTIVE for command %s\n",
                   iwl_get_cmd_string(trans, cmd->id));
    status = ZX_ERR_TIMED_OUT;

    iwl_force_nmi(trans);
    iwl_trans_fw_error(trans);

    goto cancel;
  }

  if (test_bit(STATUS_FW_ERROR, &trans->status)) {
    iwl_trans_pcie_dump_regs(trans);
    IWL_ERR(trans, "FW error in SYNC CMD %s\n", iwl_get_cmd_string(trans, cmd->id));
    backtrace_request();
    status = ZX_ERR_IO;
    goto cancel;
  }

  if (!(cmd->flags & CMD_SEND_IN_RFKILL) && test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
    IWL_DEBUG_RF_KILL(trans, "RFKILL in SYNC CMD... no rsp\n");
    status = ZX_ERR_BAD_STATE;
    goto cancel;
  }

  if ((cmd->flags & CMD_WANT_SKB) && !cmd->resp_pkt) {
    IWL_ERR(trans, "Error: Response NULL in '%s'\n", iwl_get_cmd_string(trans, cmd->id));
    status = ZX_ERR_IO;
    goto cancel;
  }

  return ZX_OK;

cancel:
  if (cmd->flags & CMD_WANT_SKB) {
    /*
     * Cancel the CMD_WANT_SKB flag for the cmd in the
     * TX cmd queue. Otherwise in case the cmd comes
     * in later, it will possibly set an invalid
     * address (cmd->meta.source).
     */
    txq->entries[cmd_idx].meta.flags &= ~CMD_WANT_SKB;
  }

  if (cmd->resp_pkt) {
    iwl_free_resp(cmd);
    cmd->resp_pkt = NULL;
  }

  return status;
}

int iwl_trans_pcie_send_hcmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd) {
  /* Make sure the NIC is still alive in the bus */
  if (test_bit(STATUS_TRANS_DEAD, &trans->status)) {
    return ZX_ERR_BAD_STATE;
  }

  if (!(cmd->flags & CMD_SEND_IN_RFKILL) && test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
    IWL_DEBUG_RF_KILL(trans, "Dropping CMD 0x%x: RF KILL\n", cmd->id);
    return ZX_ERR_BAD_STATE;
  }

  IWL_DEBUG_TX(trans, "HCMD: iwl_trans_pcie_send_hcmd( %s ) len=%4d,%4d %s %s [%s %s, %s %s]\n",
               iwl_get_cmd_string(trans, cmd->id), cmd->len[0], cmd->len[1],
               cmd->flags & CMD_ASYNC ? "ASYNC" : " SYNC",
               cmd->flags & CMD_WANT_SKB ? "SKB" : "   ",
               cmd->dataflags[0] & IWL_HCMD_DFL_NOCOPY ? "NOCOPY" : "",
               cmd->dataflags[0] & IWL_HCMD_DFL_DUP ? "DUP" : "",
               cmd->dataflags[1] & IWL_HCMD_DFL_NOCOPY ? "NOCOPY" : "",
               cmd->dataflags[1] & IWL_HCMD_DFL_DUP ? "DUP" : "");

  zx_status_t status;
  if (cmd->flags & CMD_ASYNC) {
    status = iwl_pcie_send_hcmd_async(trans, cmd);
  } else {
    /* We still can fail on RFKILL that can be asserted while we wait */
    status = iwl_pcie_send_hcmd_sync(trans, cmd);
  }

  return status;
}

#if 0  // NEEDS_PORTING
#ifdef CPTCFG_MAC80211_LATENCY_MEASUREMENTS
static void iwl_trans_pci_tx_lat_add_ts_write(struct sk_buff* skb) {
    s64 temp = ktime_to_ms(ktime_get());
    s64 ts_1 = ktime_to_ns(skb->tstamp) >> 32;
    s64 diff = temp - ts_1;

#if LINUX_VERSION_IS_LESS(4, 10, 0)
    skb->tstamp.tv64 += diff << 16;
#else
    skb->tstamp += diff << 16;
#endif
}
#endif

static int iwl_fill_data_tbs(struct iwl_trans* trans, struct sk_buff* skb, struct iwl_txq* txq,
                             uint8_t hdr_len, struct iwl_cmd_meta* out_meta) {
    uint16_t head_tb_len;
    int i;

    /*
     * Set up TFD's third entry to point directly to remainder
     * of skb's head, if any
     */
    head_tb_len = skb_headlen(skb) - hdr_len;

    if (head_tb_len > 0) {
        dma_addr_t tb_phys =
            dma_map_single(trans->dev, skb->data + hdr_len, head_tb_len, DMA_TO_DEVICE);
        if (unlikely(dma_mapping_error(trans->dev, tb_phys))) { return -EINVAL; }
        trace_iwlwifi_dev_tx_tb(trans->dev, skb, skb->data + hdr_len, head_tb_len);
        iwl_pcie_txq_build_tfd(trans, txq, tb_phys, head_tb_len, false);
    }

    /* set up the remaining entries to point to the data */
    for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
        const skb_frag_t* frag = &skb_shinfo(skb)->frags[i];
        dma_addr_t tb_phys;
        int tb_idx;

        if (!skb_frag_size(frag)) { continue; }

        tb_phys = skb_frag_dma_map(trans->dev, frag, 0, skb_frag_size(frag), DMA_TO_DEVICE);

        if (unlikely(dma_mapping_error(trans->dev, tb_phys))) { return -EINVAL; }
        trace_iwlwifi_dev_tx_tb(trans->dev, skb, skb_frag_address(frag), skb_frag_size(frag));
        tb_idx = iwl_pcie_txq_build_tfd(trans, txq, tb_phys, skb_frag_size(frag), false);
        if (tb_idx < 0) { return tb_idx; }

        out_meta->tbs |= BIT(tb_idx);
    }

    return 0;
}

#ifdef CONFIG_INET
struct iwl_tso_hdr_page* get_page_hdr(struct iwl_trans* trans, size_t len) {
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct iwl_tso_hdr_page* p = this_cpu_ptr(trans_pcie->tso_hdr_page);

    if (!p->page) { goto alloc; }

    /* enough room on this page */
    if (p->pos + len < (uint8_t*)page_address(p->page) + PAGE_SIZE) { return p; }

    /* We don't have enough room on this page, get a new one. */
    __free_page(p->page);

alloc:
    p->page = alloc_page(GFP_ATOMIC);
    if (!p->page) { return NULL; }
    p->pos = page_address(p->page);
    return p;
}

static void iwl_compute_pseudo_hdr_csum(void* iph, struct tcphdr* tcph, bool ipv6,
                                        unsigned int len) {
    if (ipv6) {
        struct ipv6hdr* iphv6 = iph;

        tcph->check =
            ~csum_ipv6_magic(&iphv6->saddr, &iphv6->daddr, len + tcph->doff * 4, IPPROTO_TCP, 0);
    } else {
        struct iphdr* iphv4 = iph;

        ip_send_check(iphv4);
        tcph->check =
            ~csum_tcpudp_magic(iphv4->saddr, iphv4->daddr, len + tcph->doff * 4, IPPROTO_TCP, 0);
    }
}

static int iwl_fill_data_tbs_amsdu(struct iwl_trans* trans, struct sk_buff* skb,
                                   struct iwl_txq* txq, uint8_t hdr_len,
                                   struct iwl_cmd_meta* out_meta, struct iwl_device_cmd* dev_cmd,
                                   uint16_t tb1_len) {
    struct iwl_tx_cmd* tx_cmd = (void*)dev_cmd->payload;
    struct iwl_trans_pcie* trans_pcie = txq->trans_pcie;
    struct ieee80211_hdr* hdr = (void*)skb->data;
    unsigned int snap_ip_tcp_hdrlen, ip_hdrlen, total_len, hdr_room;
    unsigned int mss = skb_shinfo(skb)->gso_size;
    uint16_t length, iv_len, amsdu_pad;
    uint8_t* start_hdr;
    struct iwl_tso_hdr_page* hdr_page;
    struct page** page_ptr;
    struct tso_t tso;

    /* if the packet is protected, then it must be CCMP or GCMP */
    BUILD_BUG_ON(IEEE80211_CCMP_HDR_LEN != IEEE80211_GCMP_HDR_LEN);
    iv_len = ieee80211_has_protected(hdr->frame_control) ? IEEE80211_CCMP_HDR_LEN : 0;

    trace_iwlwifi_dev_tx(trans->dev, skb, iwl_pcie_get_tfd(trans, txq, txq->write_ptr),
                         trans_pcie->tfd_size, &dev_cmd->hdr, IWL_FIRST_TB_SIZE + tb1_len, 0);

    ip_hdrlen = skb_transport_header(skb) - skb_network_header(skb);
    snap_ip_tcp_hdrlen = 8 + ip_hdrlen + tcp_hdrlen(skb);
    total_len = skb->len - snap_ip_tcp_hdrlen - hdr_len - iv_len;
    amsdu_pad = 0;

    /* total amount of header we may need for this A-MSDU */
    hdr_room =
        DIV_ROUND_UP(total_len, mss) * (3 + snap_ip_tcp_hdrlen + sizeof(struct ethhdr)) + iv_len;

    /* Our device supports 9 segments at most, it will fit in 1 page */
    hdr_page = get_page_hdr(trans, hdr_room);
    if (!hdr_page) { return -ENOMEM; }

    get_page(hdr_page->page);
    start_hdr = hdr_page->pos;
    page_ptr = (void*)((uint8_t*)skb->cb + trans_pcie->page_offs);
    *page_ptr = hdr_page->page;
    memcpy(hdr_page->pos, skb->data + hdr_len, iv_len);
    hdr_page->pos += iv_len;

    /*
     * Pull the ieee80211 header + IV to be able to use TSO core,
     * we will restore it for the tx_status flow.
     */
    skb_pull(skb, hdr_len + iv_len);

    /*
     * Remove the length of all the headers that we don't actually
     * have in the MPDU by themselves, but that we duplicate into
     * all the different MSDUs inside the A-MSDU.
     */
    le16_add_cpu(&tx_cmd->len, -snap_ip_tcp_hdrlen);

    tso_start(skb, &tso);

    while (total_len) {
        /* this is the data left for this subframe */
        unsigned int data_left = min_t(unsigned int, mss, total_len);
        struct sk_buff* csum_skb = NULL;
        unsigned int hdr_tb_len;
        dma_addr_t hdr_tb_phys;
        struct tcphdr* tcph;
        uint8_t *iph, *subf_hdrs_start = hdr_page->pos;

        total_len -= data_left;

        memset(hdr_page->pos, 0, amsdu_pad);
        hdr_page->pos += amsdu_pad;
        amsdu_pad = (4 - (sizeof(struct ethhdr) + snap_ip_tcp_hdrlen + data_left)) & 0x3;
        ether_addr_copy(hdr_page->pos, ieee80211_get_DA(hdr));
        hdr_page->pos += ETH_ALEN;
        ether_addr_copy(hdr_page->pos, ieee80211_get_SA(hdr));
        hdr_page->pos += ETH_ALEN;

        length = snap_ip_tcp_hdrlen + data_left;
        *((__be16*)hdr_page->pos) = cpu_to_be16(length);
        hdr_page->pos += sizeof(length);

        /*
         * This will copy the SNAP as well which will be considered
         * as MAC header.
         */
        tso_build_hdr(skb, hdr_page->pos, &tso, data_left, !total_len);
        iph = hdr_page->pos + 8;
        tcph = (void*)(iph + ip_hdrlen);

        /* For testing on current hardware only */
        if (trans_pcie->sw_csum_tx) {
            csum_skb = alloc_skb(data_left + tcp_hdrlen(skb), GFP_ATOMIC);
            if (!csum_skb) { return -ENOMEM; }

            iwl_compute_pseudo_hdr_csum(iph, tcph, skb->protocol == htons(ETH_P_IPV6), data_left);

            skb_put_data(csum_skb, tcph, tcp_hdrlen(skb));
            skb_reset_transport_header(csum_skb);
            csum_skb->csum_start = (unsigned char*)tcp_hdr(csum_skb) - csum_skb->head;
        }

        hdr_page->pos += snap_ip_tcp_hdrlen;

        hdr_tb_len = hdr_page->pos - start_hdr;
        hdr_tb_phys = dma_map_single(trans->dev, start_hdr, hdr_tb_len, DMA_TO_DEVICE);
        if (unlikely(dma_mapping_error(trans->dev, hdr_tb_phys))) {
            dev_kfree_skb(csum_skb);
            return -EINVAL;
        }
        iwl_pcie_txq_build_tfd(trans, txq, hdr_tb_phys, hdr_tb_len, false);
        trace_iwlwifi_dev_tx_tb(trans->dev, skb, start_hdr, hdr_tb_len);
        /* add this subframe's headers' length to the tx_cmd */
        le16_add_cpu(&tx_cmd->len, hdr_page->pos - subf_hdrs_start);

        /* prepare the start_hdr for the next subframe */
        start_hdr = hdr_page->pos;

        /* put the payload */
        while (data_left) {
            unsigned int size = min_t(unsigned int, tso.size, data_left);
            dma_addr_t tb_phys;

            if (trans_pcie->sw_csum_tx) { skb_put_data(csum_skb, tso.data, size); }

            tb_phys = dma_map_single(trans->dev, tso.data, size, DMA_TO_DEVICE);
            if (unlikely(dma_mapping_error(trans->dev, tb_phys))) {
                dev_kfree_skb(csum_skb);
                return -EINVAL;
            }

            iwl_pcie_txq_build_tfd(trans, txq, tb_phys, size, false);
            trace_iwlwifi_dev_tx_tb(trans->dev, skb, tso.data, size);

            data_left -= size;
            tso_build_data(skb, &tso, size);
        }

        /* For testing on early hardware only */
        if (trans_pcie->sw_csum_tx) {
            __wsum csum;

            csum = skb_checksum(csum_skb, skb_checksum_start_offset(csum_skb),
                                csum_skb->len - skb_checksum_start_offset(csum_skb), 0);
            dev_kfree_skb(csum_skb);
            dma_sync_single_for_cpu(trans->dev, hdr_tb_phys, hdr_tb_len, DMA_TO_DEVICE);
            tcph->check = csum_fold(csum);
            dma_sync_single_for_device(trans->dev, hdr_tb_phys, hdr_tb_len, DMA_TO_DEVICE);
        }
    }

    /* re -add the WiFi header and IV */
    skb_push(skb, hdr_len + iv_len);

    return 0;
}
#else /* CONFIG_INET */
static int iwl_fill_data_tbs_amsdu(struct iwl_trans* trans, struct sk_buff* skb,
                                   struct iwl_txq* txq, uint8_t hdr_len,
                                   struct iwl_cmd_meta* out_meta, struct iwl_device_cmd* dev_cmd,
                                   uint16_t tb1_len) {
    /* No A-MSDU without CONFIG_INET */
    WARN_ON(1);

    return -1;
}
#endif /* CONFIG_INET */
#endif  // NEEDS_PORTING

int iwl_trans_pcie_tx(struct iwl_trans* trans, const wlan_tx_packet_t* pkt,
                      const struct iwl_device_cmd* dev_cmd, int txq_id) {
#if 0  // NEEDS_PORTING
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct ieee80211_hdr* hdr;
    struct iwl_tx_cmd* tx_cmd = (struct iwl_tx_cmd*)dev_cmd->payload;
    struct iwl_cmd_meta* out_meta;
    struct iwl_txq* txq;
    dma_addr_t tb0_phys, tb1_phys, scratch_phys;
    void* tb1_addr;
    void* tfd;
    uint16_t len, tb1_len;
    bool wait_write_ptr;
    __le16 fc;
    uint8_t hdr_len;
    uint16_t wifi_seq;
    bool amsdu;

    txq = trans_pcie->txq[txq_id];

    if (WARN_ONCE(!test_bit(txq_id, trans_pcie->queue_used), "TX on unused queue %d\n", txq_id)) {
        return -EINVAL;
    }

    if (unlikely(trans_pcie->sw_csum_tx && skb->ip_summed == CHECKSUM_PARTIAL)) {
        int offs = skb_checksum_start_offset(skb);
        int csum_offs = offs + skb->csum_offset;
        __wsum csum;

        if (skb_ensure_writable(skb, csum_offs + sizeof(__sum16))) { return -1; }

        csum = skb_checksum(skb, offs, skb->len - offs, 0);
        *(__sum16*)(skb->data + csum_offs) = csum_fold(csum);

        skb->ip_summed = CHECKSUM_UNNECESSARY;
    }

    if (skb_is_nonlinear(skb) && skb_shinfo(skb)->nr_frags > IWL_PCIE_MAX_FRAGS(trans_pcie) &&
        __skb_linearize(skb)) {
        return -ENOMEM;
    }

    /* mac80211 always puts the full header into the SKB's head,
     * so there's no need to check if it's readable there
     */
    hdr = (struct ieee80211_hdr*)skb->data;
    fc = hdr->frame_control;
    hdr_len = ieee80211_hdrlen(fc);

    spin_lock(&txq->lock);

    if (iwl_queue_space(trans, txq) < txq->high_mark) {
        iwl_stop_queue(trans, txq);

        /* don't put the packet on the ring, if there is no room */
        if (unlikely(iwl_queue_space(trans, txq) < 3)) {
            struct iwl_device_cmd** dev_cmd_ptr;

            dev_cmd_ptr = (void*)((uint8_t*)skb->cb + trans_pcie->dev_cmd_offs);

            *dev_cmd_ptr = dev_cmd;
            __skb_queue_tail(&txq->overflow_q, skb);

            spin_unlock(&txq->lock);
            return 0;
        }
    }

    /* In AGG mode, the index in the ring must correspond to the WiFi
     * sequence number. This is a HW requirements to help the SCD to parse
     * the BA.
     * Check here that the packets are in the right place on the ring.
     */
    wifi_seq = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
    WARN_ONCE(txq->ampdu && (wifi_seq & 0xff) != txq->write_ptr, "Q: %d WiFi Seq %d tfdNum %d",
              txq_id, wifi_seq, txq->write_ptr);

    /* Set up driver data for this TFD */
    txq->entries[txq->write_ptr].skb = skb;
    txq->entries[txq->write_ptr].cmd = dev_cmd;

    dev_cmd->hdr.sequence =
        cpu_to_le16((uint16_t)(QUEUE_TO_SEQ(txq_id) | INDEX_TO_SEQ(txq->write_ptr)));

    tb0_phys = iwl_pcie_get_first_tb_dma(txq, txq->write_ptr);
    scratch_phys = tb0_phys + sizeof(struct iwl_cmd_header) + offsetof(struct iwl_tx_cmd, scratch);

    tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
    tx_cmd->dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

    /* Set up first empty entry in queue's array of Tx/cmd buffers */
    out_meta = &txq->entries[txq->write_ptr].meta;
    out_meta->flags = 0;

    /*
     * The second TB (tb1) points to the remainder of the TX command
     * and the 802.11 header - dword aligned size
     * (This calculation modifies the TX command, so do it before the
     * setup of the first TB)
     */
    len = sizeof(struct iwl_tx_cmd) + sizeof(struct iwl_cmd_header) + hdr_len - IWL_FIRST_TB_SIZE;
    /* do not align A-MSDU to dword as the subframe header aligns it */
    amsdu = ieee80211_is_data_qos(fc) &&
            (*ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_A_MSDU_PRESENT);
    if (trans_pcie->sw_csum_tx || !amsdu) {
        tb1_len = ALIGN(len, 4);
        /* Tell NIC about any 2-byte padding after MAC header */
        if (tb1_len != len) { tx_cmd->tx_flags |= cpu_to_le32(TX_CMD_FLG_MH_PAD); }
    } else {
        tb1_len = len;
    }

    /*
     * The first TB points to bi-directional DMA data, we'll
     * memcpy the data into it later.
     */
    iwl_pcie_txq_build_tfd(trans, txq, tb0_phys, IWL_FIRST_TB_SIZE, true);

    /* there must be data left over for TB1 or this code must be changed */
    BUILD_BUG_ON(sizeof(struct iwl_tx_cmd) < IWL_FIRST_TB_SIZE);

    /* map the data for TB1 */
    tb1_addr = ((uint8_t*)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
    tb1_phys = dma_map_single(trans->dev, tb1_addr, tb1_len, DMA_TO_DEVICE);
    if (unlikely(dma_mapping_error(trans->dev, tb1_phys))) { goto out_err; }
    iwl_pcie_txq_build_tfd(trans, txq, tb1_phys, tb1_len, false);

    trace_iwlwifi_dev_tx(trans->dev, skb, iwl_pcie_get_tfd(trans, txq, txq->write_ptr),
                         trans_pcie->tfd_size, &dev_cmd->hdr, IWL_FIRST_TB_SIZE + tb1_len, hdr_len);

    /*
     * If gso_size wasn't set, don't give the frame "amsdu treatment"
     * (adding subframes, etc.).
     * This can happen in some testing flows when the amsdu was already
     * pre-built, and we just need to send the resulting skb.
     */
    if (amsdu && skb_shinfo(skb)->gso_size) {
        if (unlikely(
                iwl_fill_data_tbs_amsdu(trans, skb, txq, hdr_len, out_meta, dev_cmd, tb1_len))) {
            goto out_err;
        }
    } else {
        struct sk_buff* frag;

        if (unlikely(iwl_fill_data_tbs(trans, skb, txq, hdr_len, out_meta))) { goto out_err; }

        skb_walk_frags(skb, frag) {
            if (unlikely(iwl_fill_data_tbs(trans, frag, txq, 0, out_meta))) { goto out_err; }
        }
    }

    /* building the A-MSDU might have changed this data, so memcpy it now */
    memcpy(&txq->first_tb_bufs[txq->write_ptr], dev_cmd, IWL_FIRST_TB_SIZE);

    tfd = iwl_pcie_get_tfd(trans, txq, txq->write_ptr);
    /* Set up entry for this TFD in Tx byte-count array */
    iwl_pcie_txq_update_byte_cnt_tbl(trans, txq, le16_to_cpu(tx_cmd->len),
                                     iwl_pcie_tfd_get_num_tbs(trans, tfd));

    wait_write_ptr = ieee80211_has_morefrags(fc);

    /* start timer if queue currently empty */
    if (txq->read_ptr == txq->write_ptr) {
        if (txq->wd_timeout) {
            /*
             * If the TXQ is active, then set the timer, if not,
             * set the timer in remainder so that the timer will
             * be armed with the right value when the station will
             * wake up.
             */
            if (!txq->frozen) {
                mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);
            } else {
                txq->frozen_expiry_remainder = txq->wd_timeout;
            }
        }
        IWL_DEBUG_RPM(trans, "Q: %d first tx - take ref\n", txq->id);
        iwl_trans_ref(trans);
    }

    /* Tell device the write index *just past* this latest filled TFD */
    txq->write_ptr = iwl_queue_inc_wrap(trans, txq->write_ptr);
    if (!wait_write_ptr) { iwl_pcie_txq_inc_wr_ptr(trans, txq); }

    /*
     * At this point the frame is "transmitted" successfully
     * and we will get a TX status notification eventually.
     */
#ifdef CPTCFG_MAC80211_LATENCY_MEASUREMENTS
    iwl_trans_pci_tx_lat_add_ts_write(skb);
#endif
    spin_unlock(&txq->lock);
    return 0;
out_err:
    iwl_pcie_tfd_unmap(trans, out_meta, txq, txq->write_ptr);
    spin_unlock(&txq->lock);
    return -1;
#endif  // NEEDS_PORTING
  IWL_ERR(trans, "%s needs porting\n", __FUNCTION__);
  return -1;
}
