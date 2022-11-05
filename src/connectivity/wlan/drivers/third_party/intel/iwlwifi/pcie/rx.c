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
#include <lib/ddk/io-buffer.h>
#include <lib/sync/condition.h>
#include <zircon/assert.h>
#include <zircon/time.h>

#if 0  // NEEDS_PORTING
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-context-info-gen3.h"
#endif
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/align.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/irq.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/memory.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/pci-fidl.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"

zx_status_t _iwl_pcie_rx_init(struct iwl_trans* trans);
void iwl_pcie_free_rbs_pool(struct iwl_trans* trans);
void iwl_pcie_rx_init_rxb_lists(struct iwl_rxq* rxq);
zx_status_t iwl_pcie_rx_alloc(struct iwl_trans* trans);

/******************************************************************************
 *
 * RX path functions
 *
 ******************************************************************************/

/*
 * Rx theory of operation
 *
 * Driver allocates a circular buffer of Receive Buffer Descriptors (RBDs),
 * each of which point to Receive Buffers to be filled by the NIC.  These get
 * used not only for Rx frames, but for any command response or notification
 * from the NIC.  The driver and NIC manage the Rx buffers by means
 * of indexes into the circular buffer.
 *
 * Rx Queue Indexes
 * The host/firmware share two index registers for managing the Rx buffers.
 *
 * The READ index maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ index is managed by the firmware once the card is enabled.
 *
 * The WRITE index maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization, the host sets up the READ queue position to the first
 * INDEX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer, it will advance the READ index
 * and fire the RX interrupt.  The driver can then query the READ index and
 * process as many packets as possible, moving the WRITE index forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated RBDs is stored in iwl->rxq->rx_free.
 *   When the interrupt handler is called, the request is processed.
 *   The page is either stolen - transferred to the upper layer
 *   or reused - added immediately to the iwl->rxq->rx_free list.
 * + When the page is stolen - the driver updates the matching queue's used
 *   count, detaches the RBD and transfers it to the queue used list.
 *   When there are two used RBDs - they are transferred to the allocator empty
 *   list. Work is then scheduled for the allocator to start allocating
 *   eight buffers.
 *   When there are another 6 used RBDs - they are transferred to the allocator
 *   empty list and the driver tries to claim the pre-allocated buffers and
 *   add them to iwl->rxq->rx_free. If it fails - it continues to claim them
 *   until ready.
 *   When there are 8+ buffers in the free list - either from allocation or from
 *   8 reused unstolen pages - restock is called to update the FW and indexes.
 * + In order to make sure the allocator always has RBDs to use for allocation
 *   the allocator has initial pool in the size of num_queues*(8-2) - the
 *   maximum missing RBDs per allocation request (request posted with 2
 *    empty RBDs, there is no guarantee when the other 6 RBDs are supplied).
 *   The queues supplies the recycle of the rest of the RBDs.
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the iwl->rxq.  The driver 'processed' index is updated.
 * + If there are no allocated buffers in iwl->rxq->rx_free,
 *   the READ INDEX is not incremented and iwl->status(RX_STALLED) is set.
 *   If there were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * iwl_rxq_alloc()            Allocates rx_free
 * iwl_pcie_rx_replenish()    Replenishes rx_free list from rx_used, and calls
 *                            iwl_pcie_rxq_restock.
 *                            Used only during initialization.
 * iwl_pcie_rxq_restock()     Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.
 * iwl_pcie_rx_allocator()     Background work for allocating pages.
 *
 * -- enable interrupts --
 * ISR - iwl_rx()             Detach iwl_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Posts and claims requests to the allocator.
 *                            Calls iwl_pcie_rxq_restock to refill any empty
 *                            slots.
 *
 * RBD life-cycle:
 *
 * Init:
 * rxq.pool -> rxq.rx_used -> rxq.rx_free -> rxq.queue
 *
 * Regular Receive interrupt:
 * Page Stolen:
 * rxq.queue -> rxq.rx_used -> allocator.rbd_empty ->
 * allocator.rbd_allocated -> rxq.rx_free -> rxq.queue
 * Page not Stolen:
 * rxq.queue -> rxq.rx_free -> rxq.queue
 * ...
 *
 */

/*
 * iwl_rxq_space - Return number of free slots available in queue.
 */
static int iwl_rxq_space(const struct iwl_rxq* rxq) {
  /* Make sure rx queue size is a power of 2 */
  WARN_ON(rxq->queue_size & (rxq->queue_size - 1));

  /*
   * There can be up to (RX_QUEUE_SIZE - 1) free slots, to avoid ambiguity
   * between empty and completely full queues.
   * The following is equivalent to modulo by RX_QUEUE_SIZE and is well
   * defined for negative dividends.
   */
  return (rxq->read - rxq->write - 1) & (rxq->queue_size - 1);
}

/*
 * iwl_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32 iwl_pcie_dma_addr2rbd_ptr(dma_addr_t dma_addr) {
  return cpu_to_le32((uint32_t)(dma_addr >> 8));
}

/*
 * iwl_pcie_rx_stop - stops the Rx DMA
 */
int iwl_pcie_rx_stop(struct iwl_trans* trans) {
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    /* TODO: remove this for 22560 once fw does it */
    iwl_write_prph(trans, RFH_RXF_DMA_CFG_GEN3, 0);
    return iwl_poll_prph_bit(trans, RFH_GEN_STATUS_GEN3, RXF_DMA_IDLE, RXF_DMA_IDLE, ZX_MSEC(1),
                             NULL);
  } else if (trans->cfg->mq_rx_supported) {
    iwl_write_prph(trans, RFH_RXF_DMA_CFG, 0);
    return iwl_poll_prph_bit(trans, RFH_GEN_STATUS, RXF_DMA_IDLE, RXF_DMA_IDLE, ZX_MSEC(1), NULL);
  } else {
    iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
    return iwl_poll_direct_bit(trans, FH_MEM_RSSR_RX_STATUS_REG, FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
                               ZX_MSEC(1), NULL);
  }
}

/*
 * iwl_pcie_rxq_inc_wr_ptr - Update the write pointer for the RX queue
 */
static void iwl_pcie_rxq_inc_wr_ptr(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  uint32_t reg;

  iwl_assert_lock_held(&rxq->lock);

  /*
   * explicitly wake up the NIC if:
   * 1. shadow registers aren't enabled
   * 2. there is a chance that the NIC is asleep
   */
  if (!trans->cfg->base_params->shadow_reg_enable && test_bit(STATUS_TPOWER_PMI, &trans->status)) {
    reg = iwl_read32(trans, CSR_UCODE_DRV_GP1);

    if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
      IWL_DEBUG_INFO(trans, "Rx queue requesting wakeup, GP1 = 0x%x\n", reg);
      iwl_set_bit(trans, CSR_GP_CNTRL, BIT(trans->cfg->csr->flag_mac_access_req));
      rxq->need_update = true;
      return;
    }
  }

  rxq->write_actual = ROUND_DOWN(rxq->write, 8);
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560)
    iwl_write32(trans, HBUS_TARG_WRPTR, (rxq->write_actual | ((FIRST_RX_QUEUE + rxq->id) << 16)));
  else if (trans->cfg->mq_rx_supported) {
    iwl_write32(trans, RFH_Q_FRBDCB_WIDX_TRG(rxq->id), rxq->write_actual);
  } else {
    iwl_write32(trans, FH_RSCSR_CHNL0_WPTR, rxq->write_actual);
  }
}

static void iwl_pcie_rxq_check_wrptr(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int i;

  for (i = 0; i < trans->num_rx_queues; i++) {
    struct iwl_rxq* rxq = &trans_pcie->rxq[i];

    if (!rxq->need_update) {
      continue;
    }
    mtx_lock(&rxq->lock);
    iwl_pcie_rxq_inc_wr_ptr(trans, rxq);
    rxq->need_update = false;
    mtx_unlock(&rxq->lock);
  }
}

static void iwl_pcie_restock_bd(struct iwl_trans* trans, struct iwl_rxq* rxq,
                                struct iwl_rx_mem_buffer* rxb) {
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    struct iwl_rx_transfer_desc* bd = iwl_iobuf_virtual(rxq->descriptors);

    bd[rxq->write].type_n_size = cpu_to_le32((IWL_RX_TD_TYPE & IWL_RX_TD_TYPE_MSK) |
                                             ((IWL_RX_TD_SIZE_2K >> 8) & IWL_RX_TD_SIZE_MSK));
    bd[rxq->write].addr = cpu_to_le64(iwl_iobuf_physical(rxb->io_buf));
    bd[rxq->write].rbid = cpu_to_le16(rxb->vid);
  } else {
    __le64* bd = iwl_iobuf_virtual(rxq->descriptors);

    bd[rxq->write] = cpu_to_le64(iwl_iobuf_physical(rxb->io_buf) | rxb->vid);
  }

  IWL_DEBUG_RX(trans, "Assigned virtual RB ID %u to queue %d index %d\n", (uint32_t)rxb->vid,
               rxq->id, rxq->write);
}

/*
 * iwl_pcie_rxmq_restock - restock implementation for multi-queue rx
 */
static void iwl_pcie_rxmq_restock(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  struct iwl_rx_mem_buffer* rxb;

  /*
   * If the device isn't enabled - no need to try to add buffers...
   * This can happen when we stop the device and still have an interrupt
   * pending. We stop the APM before we sync the interrupts because we
   * have to (see comment there). On the other hand, since the APM is
   * stopped, we cannot access the HW (in particular not prph).
   * So don't try to restock if the APM has been already stopped.
   */
  if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
    return;
  }

  mtx_lock(&rxq->lock);
  while (rxq->free_count) {
    /* Get next free Rx buffer, remove from free list */
    rxb = list_remove_head_type(&rxq->rx_free, struct iwl_rx_mem_buffer, list);
    rxb->invalid = false;
    /* 12 first bits are expected to be empty */
    WARN_ON(iwl_iobuf_physical(rxb->io_buf) & DMA_BIT_MASK(12));
    /* Point to Rx buffer via next RBD in circular buffer */
    iwl_pcie_restock_bd(trans, rxq, rxb);
    rxq->write = (rxq->write + 1) & MQ_RX_TABLE_MASK;
    rxq->free_count--;
  }
  mtx_unlock(&rxq->lock);

  /*
   * If we've added more space for the firmware to place data, tell it.
   * Increment device's write pointer in multiples of 8.
   */
  if (rxq->write_actual != (rxq->write & ~0x7)) {
    mtx_lock(&rxq->lock);
    iwl_pcie_rxq_inc_wr_ptr(trans, rxq);
    mtx_unlock(&rxq->lock);
  }
}

/*
 * iwl_pcie_rxsq_restock - restock implementation for single queue rx
 */
static void iwl_pcie_rxsq_restock(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  struct iwl_rx_mem_buffer* rxb;

  /*
   * If the device isn't enabled - not need to try to add buffers...
   * This can happen when we stop the device and still have an interrupt
   * pending. We stop the APM before we sync the interrupts because we
   * have to (see comment there). On the other hand, since the APM is
   * stopped, we cannot access the HW (in particular not prph).
   * So don't try to restock if the APM has been already stopped.
   */
  if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
    return;
  }

  mtx_lock(&rxq->lock);
  while ((iwl_rxq_space(rxq) > 0) && (rxq->free_count)) {
    __le32* bd = (__le32*)(iwl_iobuf_virtual(rxq->descriptors));
    /* The overwritten rxb must be a used one */
    rxb = rxq->queue[rxq->write];
    ZX_ASSERT(!(rxb && rxb->io_buf));

    /* Get next free Rx buffer, remove from free list */
    rxb = list_remove_head_type(&rxq->rx_free, struct iwl_rx_mem_buffer, list);
    rxb->invalid = false;

    /* Point to Rx buffer via next RBD in circular buffer */
    bd[rxq->write] = iwl_pcie_dma_addr2rbd_ptr(iwl_iobuf_physical(rxb->io_buf));
    rxq->queue[rxq->write] = rxb;
    rxq->write = (rxq->write + 1) & RX_QUEUE_MASK;
    rxq->free_count--;
  }
  mtx_unlock(&rxq->lock);

  /* If we've added more space for the firmware to place data, tell it.
   * Increment device's write pointer in multiples of 8. */
  if (rxq->write_actual != (rxq->write & ~0x7)) {
    mtx_lock(&rxq->lock);
    iwl_pcie_rxq_inc_wr_ptr(trans, rxq);
    mtx_unlock(&rxq->lock);
  }
}

/*
 * iwl_pcie_rxq_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
static void iwl_pcie_rxq_restock(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  if (trans->cfg->mq_rx_supported) {
    iwl_pcie_rxmq_restock(trans, rxq);
  } else {
    iwl_pcie_rxsq_restock(trans, rxq);
  }
}

void iwl_pcie_free_rbs_pool(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int i;

  for (i = 0; i < RX_POOL_SIZE; i++) {
    if (!trans_pcie->rx_pool[i].io_buf) {
      continue;
    }
    iwl_iobuf_release(trans_pcie->rx_pool[i].io_buf);
    trans_pcie->rx_pool[i].io_buf = NULL;
  }
}

static int iwl_pcie_free_bd_size(struct iwl_trans* trans, bool use_rx_td) {
  struct iwl_rx_transfer_desc* rx_td;

  if (use_rx_td) {
    return sizeof(*rx_td);
  } else {
    return trans->cfg->mq_rx_supported ? sizeof(__le64) : sizeof(__le32);
  }
}

static void iwl_pcie_free_rxq_dma(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  iwl_iobuf_release(rxq->descriptors);
  rxq->descriptors = NULL;
  iwl_iobuf_release(rxq->rb_status);
  rxq->rb_status = NULL;

  if (rxq->used_descriptors) {
    iwl_iobuf_release(rxq->used_descriptors);
  }
  rxq->used_descriptors = NULL;

  if (trans->cfg->device_family < IWL_DEVICE_FAMILY_22560) {
    return;
  }

  // The following code is only used for the 22560+ device families. Which are not currently
  // supported.
#if 0   // NEEDS_PORTING
  if (rxq->tr_tail) {
    dma_free_coherent(dev, sizeof(__le16), rxq->tr_tail, rxq->tr_tail_dma);
  }
  rxq->tr_tail_dma = 0;
  rxq->tr_tail = NULL;

  if (rxq->cr_tail) {
    dma_free_coherent(dev, sizeof(__le16), rxq->cr_tail, rxq->cr_tail_dma);
  }
  rxq->cr_tail_dma = 0;
  rxq->cr_tail = NULL;
#endif  // NEEDS_PORTING
}

static zx_status_t iwl_pcie_alloc_rxq_dma(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int i;
  int free_size;
  bool use_rx_td = (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560);

  mtx_init(&rxq->lock, mtx_plain);
  if (trans->cfg->mq_rx_supported) {
    rxq->queue_size = MQ_RX_TABLE_SIZE;
  } else {
    rxq->queue_size = RX_QUEUE_SIZE;
  }

  free_size = iwl_pcie_free_bd_size(trans, use_rx_td);

  /*
   * Allocate the circular buffer of Read Buffer Descriptors
   * (RBDs)
   */
  zx_status_t status = iwl_iobuf_allocate_contiguous(
      &trans_pcie->pci_dev->dev, free_size * rxq->queue_size, &rxq->descriptors);
  if (status != ZX_OK) {
    goto err;
  }

  if (trans->cfg->mq_rx_supported) {
    status = iwl_iobuf_allocate_contiguous(
        &trans_pcie->pci_dev->dev,
        (use_rx_td ? sizeof(*rxq->cd) : sizeof(__le32)) * rxq->queue_size, &rxq->used_descriptors);
    if (status != ZX_OK) {
      goto err;
    }
    rxq->used_bd = iwl_iobuf_virtual(rxq->used_descriptors);
  }

  /* Allocate the driver's pointer to receive buffer status */
  status = iwl_iobuf_allocate_contiguous(&trans_pcie->pci_dev->dev,
                                         use_rx_td ? sizeof(__le16) : sizeof(struct iwl_rb_status),
                                         &rxq->rb_status);
  if (status != ZX_OK) {
    goto err;
  }

  if (!use_rx_td) {
    return ZX_OK;
  }

  // The following code is only used for the 22560+ device families. Which are not currently
  // supported.
#if 0  // NEEDS_PORTING
  /* Allocate the driver's pointer to TR tail */
  rxq->tr_tail = dma_zalloc_coherent(dev, sizeof(__le16), &rxq->tr_tail_dma, GFP_KERNEL);
  if (!rxq->tr_tail) {
    goto err;
  }

  /* Allocate the driver's pointer to CR tail */
  rxq->cr_tail = dma_zalloc_coherent(dev, sizeof(__le16), &rxq->cr_tail_dma, GFP_KERNEL);
  if (!rxq->cr_tail) {
    goto err;
  }
  /*
   * W/A 22560 device step Z0 must be non zero bug
   * TODO: remove this when stop supporting Z0
   */
  *rxq->cr_tail = cpu_to_le16(500);
#endif

  return ZX_OK;

err:
  for (i = 0; i < trans->num_rx_queues; i++) {
    struct iwl_rxq* rxq = &trans_pcie->rxq[i];

    iwl_pcie_free_rxq_dma(trans, rxq);
  }
  free(trans_pcie->rxq);

  return status;
}

zx_status_t iwl_pcie_rx_alloc(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (WARN_ON(trans_pcie->rxq)) {
    return ZX_ERR_BAD_STATE;
  }

  trans_pcie->rxq = calloc(trans->num_rx_queues, sizeof(struct iwl_rxq));
  if (!trans_pcie->rxq) {
    return ZX_ERR_NO_MEMORY;
  }

  for (int i = 0; i < trans->num_rx_queues; i++) {
    struct iwl_rxq* rxq = &trans_pcie->rxq[i];

    zx_status_t status = iwl_pcie_alloc_rxq_dma(trans, rxq);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

static void iwl_pcie_rx_hw_init(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t rb_size;
  unsigned long flags;
  const uint32_t rfdnlog = RX_QUEUE_SIZE_LOG; /* 256 RBDs */

  switch (trans_pcie->rx_buf_size) {
    case IWL_AMSDU_4K:
      rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;
      break;
    case IWL_AMSDU_8K:
      rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
      break;
    case IWL_AMSDU_12K:
      rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_12K;
      break;
    default:
      WARN_ON(1);
      rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;
  }

  if (!iwl_trans_grab_nic_access(trans, &flags)) {
    return;
  }

  /* Stop Rx DMA */
  iwl_write32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
  /* reset and flush pointers */
  iwl_write32(trans, FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
  iwl_write32(trans, FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
  iwl_write32(trans, FH_RSCSR_CHNL0_RDPTR, 0);

  /* Reset driver's Rx queue write index */
  iwl_write32(trans, FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

  /* Tell device where to find RBD circular buffer in DRAM */
  iwl_write32(trans, FH_RSCSR_CHNL0_RBDCB_BASE_REG,
              (uint32_t)(iwl_iobuf_physical(rxq->descriptors) >> 8));

  /* Tell device where in DRAM to update its Rx status */
  iwl_write32(trans, FH_RSCSR_CHNL0_STTS_WPTR_REG, iwl_iobuf_physical(rxq->rb_status) >> 4);

  /* Enable Rx DMA
   * FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY is set because of HW bug in
   *      the credit mechanism in 5000 HW RX FIFO
   * Direct rx interrupts to hosts
   * Rx buffer size 4 or 8k or 12k
   * RB timeout 0x10
   * 256 RBDs
   */
  iwl_write32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG,
              FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL | FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY |
                  FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL | rb_size |
                  (RX_RB_TIMEOUT << FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
                  (rfdnlog << FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS));

  iwl_trans_release_nic_access(trans, &flags);

  /* Set interrupt coalescing timer to default (2048 usecs) */
  iwl_write8(trans, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);

  /* W/A for interrupt coalescing bug in 7260 and 3160 */
  if (trans->cfg->host_interrupt_operation_mode) {
    iwl_set_bit(trans, CSR_INT_COALESCING, IWL_HOST_INT_OPER_MODE);
  }
}

static void iwl_pcie_rx_mq_hw_init(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t rb_size, enabled = 0;
  unsigned long flags;
  int i;

  switch (trans_pcie->rx_buf_size) {
    case IWL_AMSDU_2K:
      rb_size = RFH_RXF_DMA_RB_SIZE_2K;
      break;
    case IWL_AMSDU_4K:
      rb_size = RFH_RXF_DMA_RB_SIZE_4K;
      break;
    case IWL_AMSDU_8K:
      rb_size = RFH_RXF_DMA_RB_SIZE_8K;
      break;
    case IWL_AMSDU_12K:
      rb_size = RFH_RXF_DMA_RB_SIZE_12K;
      break;
    default:
      WARN_ON(1);
      rb_size = RFH_RXF_DMA_RB_SIZE_4K;
  }

  if (!iwl_trans_grab_nic_access(trans, &flags)) {
    return;
  }

  /* Stop Rx DMA */
  iwl_write_prph_no_grab(trans, RFH_RXF_DMA_CFG, 0);
  /* disable free amd used rx queue operation */
  iwl_write_prph_no_grab(trans, RFH_RXF_RXQ_ACTIVE, 0);

  for (i = 0; i < trans->num_rx_queues; i++) {
    /* Tell device where to find RBD free table in DRAM */
    iwl_write_prph64_no_grab(trans, RFH_Q_FRBDCB_BA_LSB(i),
                             iwl_iobuf_physical(trans_pcie->rxq[i].descriptors));
    /* Tell device where to find RBD used table in DRAM */
    iwl_write_prph64_no_grab(trans, RFH_Q_URBDCB_BA_LSB(i),
                             iwl_iobuf_physical(trans_pcie->rxq[i].used_descriptors));
    /* Tell device where in DRAM to update its Rx status */
    iwl_write_prph64_no_grab(trans, RFH_Q_URBD_STTS_WPTR_LSB(i),
                             iwl_iobuf_physical(trans_pcie->rxq[i].rb_status));
    /* Reset device indice tables */
    iwl_write_prph_no_grab(trans, RFH_Q_FRBDCB_WIDX(i), 0);
    iwl_write_prph_no_grab(trans, RFH_Q_FRBDCB_RIDX(i), 0);
    iwl_write_prph_no_grab(trans, RFH_Q_URBDCB_WIDX(i), 0);

    enabled |= BIT(i) | BIT(i + 16);
  }

  /*
   * Enable Rx DMA
   * Rx buffer size 4 or 8k or 12k
   * Min RB size 4 or 8
   * Drop frames that exceed RB size
   * 512 RBDs
   */
  iwl_write_prph_no_grab(trans, RFH_RXF_DMA_CFG,
                         RFH_DMA_EN_ENABLE_VAL | rb_size | RFH_RXF_DMA_MIN_RB_4_8 |
                             RFH_RXF_DMA_DROP_TOO_LARGE_MASK | RFH_RXF_DMA_RBDCB_SIZE_512);

  /*
   * Activate DMA snooping.
   * Set RX DMA chunk size to 64B for IOSF and 128B for PCIe
   * Default queue is 0
   */
  iwl_write_prph_no_grab(
      trans, RFH_GEN_CFG,
      RFH_GEN_CFG_RFH_DMA_SNOOP | RFH_GEN_CFG_VAL(DEFAULT_RXQ_NUM, 0) |
          RFH_GEN_CFG_SERVICE_DMA_SNOOP |
          RFH_GEN_CFG_VAL(RB_CHUNK_SIZE, trans->cfg->integrated ? RFH_GEN_CFG_RB_CHUNK_SIZE_64
                                                                : RFH_GEN_CFG_RB_CHUNK_SIZE_128));
  /* Enable the relevant rx queues */
  iwl_write_prph_no_grab(trans, RFH_RXF_RXQ_ACTIVE, enabled);

  iwl_trans_release_nic_access(trans, &flags);

  /* Set interrupt coalescing timer to default (2048 usecs) */
  iwl_write8(trans, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);
}

#if 0   // NEEDS_PORTING
int iwl_pcie_dummy_napi_poll(struct napi_struct* napi, int budget) {
  WARN_ON(1);
  return 0;
}
#endif  // NEEDS_PORTING

zx_status_t _iwl_pcie_rx_init(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_rxq* def_rxq;
  int i, queue_size;

  if (!trans_pcie->rxq) {
    zx_status_t status = iwl_pcie_rx_alloc(trans);
    if (status != ZX_OK) {
      return status;
    }
  }
  def_rxq = trans_pcie->rxq;

  /* free all first - we might be reconfigured for a different size */
  iwl_pcie_free_rbs_pool(trans);

  for (i = 0; i < RX_QUEUE_SIZE; i++) {
    def_rxq->queue[i] = NULL;
  }

  for (i = 0; i < trans->num_rx_queues; i++) {
    struct iwl_rxq* rxq = &trans_pcie->rxq[i];

    rxq->id = i;

    mtx_lock(&rxq->lock);
    /*
     * Set read write pointer to reflect that we have processed
     * and used all buffers, but have not restocked the Rx queue
     * with fresh buffers
     */
    rxq->read = 0;
    rxq->write = 0;
    rxq->write_actual = 0;
    memset(iwl_iobuf_virtual(rxq->rb_status), 0,
           (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) ? sizeof(__le16)
                                                                  : sizeof(struct iwl_rb_status));

    list_initialize(&rxq->rx_free);
    rxq->free_count = 0;

#if 0   // NEEDS_PORTING
    if (!rxq->napi.poll) {
      netif_napi_add(&trans_pcie->napi_dev, &rxq->napi, iwl_pcie_dummy_napi_poll, 64);
    }
#endif  // NEEDS_PORTING

    mtx_unlock(&rxq->lock);
  }

  const size_t buffer_size = PAGE_SIZE * (1 << trans_pcie->rx_page_order);
  queue_size = trans->cfg->mq_rx_supported ? MQ_RX_NUM_RBDS : RX_QUEUE_SIZE;
  BUILD_BUG_ON(ARRAY_SIZE(trans_pcie->global_table) != ARRAY_SIZE(trans_pcie->rx_pool));

  for (i = 0; i < queue_size; i++) {
    struct iwl_rx_mem_buffer* rxb = &trans_pcie->rx_pool[i];

    // Initialize the io buffer.
    zx_status_t status =
        iwl_iobuf_allocate_contiguous(&trans_pcie->pci_dev->dev, buffer_size, &rxb->io_buf);
    if (status != ZX_OK) {
      IWL_WARN(trans, "Failed to allocate io buffer\n");
      return status;
    }

    mtx_lock(&def_rxq->lock);
    list_add_tail(&def_rxq->rx_free, &rxb->list);
    def_rxq->free_count++;
    mtx_unlock(&def_rxq->lock);

    trans_pcie->global_table[i] = rxb;
    rxb->vid = (uint16_t)(i + 1);
    rxb->invalid = true;
  }

  return ZX_OK;
}

zx_status_t iwl_pcie_rx_init(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  int ret = _iwl_pcie_rx_init(trans);

  if (ret != ZX_OK) {
    return ret;
  }

  if (trans->cfg->mq_rx_supported) {
    iwl_pcie_rx_mq_hw_init(trans);
  } else {
    iwl_pcie_rx_hw_init(trans, trans_pcie->rxq);
  }

  iwl_pcie_rxq_restock(trans, trans_pcie->rxq);

  mtx_lock(&trans_pcie->rxq->lock);
  iwl_pcie_rxq_inc_wr_ptr(trans, trans_pcie->rxq);
  mtx_unlock(&trans_pcie->rxq->lock);

  return ZX_OK;
}

#if 0   // NEEDS_PORTING
int iwl_pcie_gen2_rx_init(struct iwl_trans* trans) {
  /* Set interrupt coalescing timer to default (2048 usecs) */
  iwl_write8(trans, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);

  /*
   * We don't configure the RFH.
   * Restock will be done at alive, after firmware configured the RFH.
   */
  return _iwl_pcie_rx_init(trans);
}
#endif  // NEEDS_PORTING

void iwl_pcie_rx_free(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  /*
   * if rxq is NULL, it means that nothing has been allocated,
   * exit now
   */
  if (!trans_pcie->rxq) {
    IWL_DEBUG_INFO(trans, "Free NULL rx context\n");
    return;
  }

  iwl_pcie_free_rbs_pool(trans);

  for (int i = 0; i < trans->num_rx_queues; i++) {
    struct iwl_rxq* rxq = &trans_pcie->rxq[i];

    iwl_pcie_free_rxq_dma(trans, rxq);

    /* Destroy mtx */
    mtx_destroy(&rxq->lock);

#if 0   // NEEDS_PORTING
    if (rxq->napi.poll) {
      netif_napi_del(&rxq->napi);
    }
#endif  // NEEDS_PORTING
  }
  free(trans_pcie->rxq);
}

static void iwl_pcie_rx_handle_rb(struct iwl_trans* trans, struct iwl_rxq* rxq,
                                  struct iwl_rx_mem_buffer* rxb, int i) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t offset = 0;

  if (WARN_ON(!rxb)) {
    return;
  }

  uint32_t max_len = iwl_iobuf_size(rxb->io_buf);
  while (offset + sizeof(uint32_t) + sizeof(struct iwl_cmd_header) < max_len) {
    struct iwl_rx_packet* pkt;
    struct iwl_rx_cmd_buffer rxcb = {
        ._offset = offset,
        ._iobuf = rxb->io_buf,
    };

#if 0   // NEEDS_PORTING
    if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
      rxcb.status = rxq->cd[i].status;
    }
#endif  // NEEDS_PORTING

    pkt = rxb_addr(&rxcb);

    if (pkt->len_n_flags == cpu_to_le32(FH_RSCSR_FRAME_INVALID)) {
      IWL_DEBUG_RX(trans, "Q %d: RB end marker at offset %d\n", rxq->id, offset);
      break;
    }

    WARN((le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_RXQ_MASK) >> FH_RSCSR_RXQ_POS != rxq->id,
         "frame on invalid queue - is on %d and indicates %d\n", rxq->id,
         (le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_RXQ_MASK) >> FH_RSCSR_RXQ_POS);

#if 0   // NEEDS_PORTING
    IWL_DEBUG_RX(trans, "Q %d: cmd at offset %d: %s (%.2x.%2x, seq 0x%x)\n", rxq->id, offset,
                 iwl_get_cmd_string(trans, iwl_cmd_id(pkt->hdr.cmd, pkt->hdr.group_id, 0)),
                 pkt->hdr.group_id, pkt->hdr.cmd, le16_to_cpu(pkt->hdr.sequence));
#endif  // NEEDS_PORTING

    int len = iwl_rx_packet_len(pkt);
    len += sizeof(uint32_t); /* account for status word */

#if 0   // NEEDS_PORTING
    trace_iwlwifi_dev_rx(trans->dev, trans, pkt, len);
    trace_iwlwifi_dev_rx_data(trans->dev, trans, pkt, len);
#endif  // NEEDS_PORTING

    /* Reclaim a command buffer only if this packet is a response
     *   to a (driver-originated) command.
     * If the packet (e.g. Rx frame) originated from uCode,
     *   there is no command buffer to reclaim.
     * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
     *   but apparently a few don't get set; catch them here. */
    bool reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME);
    if (reclaim && !pkt->hdr.group_id) {
      int i;

      for (i = 0; i < trans_pcie->n_no_reclaim_cmds; i++) {
        if (trans_pcie->no_reclaim_cmds[i] == pkt->hdr.cmd) {
          reclaim = false;
          break;
        }
      }
    }

    uint16_t sequence = le16_to_cpu(pkt->hdr.sequence);
    int index = SEQ_TO_INDEX(sequence);
    struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];
    __UNUSED int cmd_index = iwl_pcie_get_cmd_index(txq, index);

    // Only handle rx packets when the mvm has been initialed.
    //
    // In the case of pcie_test, the MVM is not required for testing so that we just ignore the RX
    // packet anyway.
    if (trans->op_mode) {
      iwl_stats_inc(IWL_STATS_CNT_CMD_FROM_FW);

      if (rxq->id == trans_pcie->def_rx_queue) {
        iwl_op_mode_rx(trans->op_mode, &rxq->napi, &rxcb);
      } else {
        iwl_op_mode_rx_rss(trans->op_mode, &rxq->napi, &rxcb, rxq->id);
      }
    }

    if (reclaim) {
      // Release the duplicated buffer (for large transmitting packets) in TX (if any).
      if (txq->entries[cmd_index].dup_io_buf) {
        iwl_iobuf_release(txq->entries[cmd_index].dup_io_buf);
        txq->entries[cmd_index].dup_io_buf = NULL;
      }

      /* Invoke any callbacks, transfer the buffer to caller,
       * and fire off the (possibly) blocking
       * iwl_trans_send_cmd()
       * as we reclaim the driver command queue */
      iwl_pcie_hcmd_complete(trans, &rxcb);
    }

    if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
      break;
    }
    offset += IWL_ALIGN(len, FH_RSCSR_FRAME_ALIGN);
  }

  // Add the buffer back to the rx_free list.
  list_add_tail(&rxq->rx_free, &rxb->list);
  rxq->free_count++;
}

static struct iwl_rx_mem_buffer* iwl_pcie_get_rxb(struct iwl_trans* trans, struct iwl_rxq* rxq,
                                                  int i) {
  struct iwl_rx_mem_buffer* rxb;

  if (!trans->cfg->mq_rx_supported) {
    rxb = rxq->queue[i];
    rxq->queue[i] = NULL;
    return rxb;
  }

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint16_t vid;

  /* used_bd is a 32/16 bit but only 12 are used to retrieve the vid */
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    vid = le16_to_cpu(rxq->cd[i].rbid) & 0x0FFF;
  } else {
    vid = le32_to_cpu(rxq->bd_32[i]) & 0x0FFF;
  }

  if (!vid || vid > ARRAY_SIZE(trans_pcie->global_table)) {
    goto out_err;
  }

  rxb = trans_pcie->global_table[vid - 1];
  if (rxb->invalid) {
    goto out_err;
  }

  IWL_DEBUG_RX(trans, "Got virtual RB ID %u\n", (uint32_t)rxb->vid);

  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    rxb->size = le32_to_cpu(rxq->cd[i].size) & IWL_RX_CD_SIZE;
  }

  rxb->invalid = true;

  return rxb;

out_err:
  IWL_WARN(trans, "Invalid rxb from HW %u\n", (uint32_t)vid);
  iwl_force_nmi(trans);
  return NULL;
}

/*
 * iwl_pcie_rx_handle - Main entry function for receiving responses from fw
 */
static void iwl_pcie_rx_handle(struct iwl_trans* trans, int queue) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_rxq* rxq = &trans_pcie->rxq[queue];
  uint32_t r, i;

  zx_time_t start_time = zx_clock_get_monotonic();

  mtx_lock(&rxq->lock);
  /* uCode's read index (stored in shared DRAM) indicates the last Rx
   * buffer that the driver may process (last buffer filled by ucode). */
  r = le16_to_cpu(iwl_get_closed_rb_stts(trans, rxq)) & 0x0FFF;
  i = rxq->read;

  /* W/A 9000 device step A0 wrap-around bug */
  r &= (rxq->queue_size - 1);

  /* Rx interrupt, but nothing sent from uCode */
  if (i == r) {
    IWL_DEBUG_RX(trans, "Q %d: HW = SW = %d\n", rxq->id, r);
  }

  while (i != r) {
    struct iwl_rx_mem_buffer* rxb;

    IWL_DEBUG_RX(trans, "Q %d: HW = %d, SW = %d\n", rxq->id, r, i);

    rxb = iwl_pcie_get_rxb(trans, rxq, i);
    if (!rxb) {
      break;
    }

    iwl_pcie_rx_handle_rb(trans, rxq, rxb, i);

    i = (i + 1) & (rxq->queue_size - 1);
  }

  /* Backtrack one entry */
  rxq->read = i;

  /* update cr tail with the rxq read pointer */
#if 0   // NEEDS_PORTING
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    *rxq->cr_tail = cpu_to_le16(r);
  }
#endif  // NEEDS_PORTING
  mtx_unlock(&rxq->lock);

#if 0   // NEEDS_PORTING
  if (rxq->napi.poll) {
    napi_gro_flush(&rxq->napi, false);
  }
#endif  // NEEDS_PORTING
  zx_duration_t rx_isr_duration = zx_time_sub_time(zx_clock_get_monotonic(), start_time);
  iwl_stats_update_rx_isr_duration(rx_isr_duration);
  iwl_pcie_rxq_restock(trans, rxq);
}

#if 0   // NEEDS_PORTING
static struct iwl_trans_pcie* iwl_pcie_get_trans_pcie(struct msix_entry* entry) {
  uint8_t queue = entry->entry;
  struct msix_entry* entries = entry - queue;

  return container_of(entries, struct iwl_trans_pcie, msix_entries[0]);
}

/*
 * iwl_pcie_rx_msix_handle - Main entry function for receiving responses from fw
 * This interrupt handler should be used with RSS queue only.
 */
irqreturn_t iwl_pcie_irq_rx_msix_handler(int irq, void* dev_id) {
  struct msix_entry* entry = dev_id;
  struct iwl_trans_pcie* trans_pcie = iwl_pcie_get_trans_pcie(entry);
  struct iwl_trans* trans = trans_pcie->trans;

  trace_iwlwifi_dev_irq_msix(trans->dev, entry, false, 0, 0);

  if (WARN_ON(entry->entry >= trans->num_rx_queues)) {
    return IRQ_NONE;
  }

  lock_map_acquire(&trans->sync_cmd_lockdep_map);

  local_bh_disable();
  iwl_pcie_rx_handle(trans, entry->entry);
  local_bh_enable();

  iwl_pcie_clear_irq(trans, entry);

  lock_map_release(&trans->sync_cmd_lockdep_map);

  return IRQ_HANDLED;
}
#endif  // NEEDS_PORTING

/*
 * iwl_pcie_irq_handle_error - called for HW or SW error interrupt from card
 */
static void iwl_pcie_irq_handle_error(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  /* W/A for WiFi/WiMAX coex and WiMAX own the RF */
  if (trans->cfg->internal_wimax_coex && !trans->cfg->apmg_not_supported &&
      (!(iwl_read_prph(trans, APMG_CLK_CTRL_REG) & APMS_CLK_VAL_MRB_FUNC_MODE) ||
       (iwl_read_prph(trans, APMG_PS_CTRL_REG) & APMG_PS_CTRL_VAL_RESET_REQ))) {
    clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
    iwl_op_mode_wimax_active(trans->op_mode);
    sync_completion_signal(&trans_pcie->wait_command_queue);
    return;
  }

  for (int i = 0; i < trans->cfg->base_params->num_of_queues; i++) {
    if (!trans_pcie->txq[i]) {
      continue;
    }
    iwl_irq_timer_stop(trans_pcie->txq[i]->stuck_timer);
  }

  /* The STATUS_FW_ERROR bit is set in this function. This must happen
   * before we wake up the command caller, to ensure a proper cleanup. */
  iwl_trans_fw_error(trans);

  clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
  sync_completion_signal(&trans_pcie->wait_command_queue);
}

static uint32_t iwl_pcie_int_cause_non_ict(struct iwl_trans* trans) {
  uint32_t inta;

  iwl_assert_lock_held(&IWL_TRANS_GET_PCIE_TRANS(trans)->irq_lock);

#if 0   // NEEDS_PORTING
  trace_iwlwifi_dev_irq(trans->dev);
#endif  // NEEDS_PORTING
  /* Discover which interrupts are active/pending */
  inta = iwl_read32(trans, CSR_INT);

  /* the thread will service interrupts and re-enable them */
  return inta;
}

/* a device (PCI-E) page is 4096 bytes long */
#define ICT_SHIFT 12
#define ICT_SIZE (1 << ICT_SHIFT)
#define ICT_COUNT (ICT_SIZE / sizeof(uint32_t))

/* interrupt handler using ict table, with this interrupt driver will
 * stop using INTA register to get device's interrupt, reading this register
 * is expensive, device will write interrupts in ICT dram table, increment
 * index then will fire interrupt to driver, driver will OR all ICT table
 * entries from current index up to table entry with 0 value. the result is
 * the interrupt we need to service, driver will set the entries back to 0 and
 * set index.
 */
uint32_t iwl_pcie_int_cause_ict(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  uint32_t* ict_table = (uint32_t*)iwl_iobuf_virtual(trans_pcie->ict_tbl);

#if 0   // NEEDS_PORTING
  trace_iwlwifi_dev_irq(trans->dev);
#endif  // NEEDS_PORTING

  /* Ignore interrupt if there's nothing in NIC to service.
   * This may be due to IRQ shared with another device,
   * or due to sporadic interrupts thrown from our NIC. */
  uint32_t read = le32_to_cpu(ict_table[trans_pcie->ict_index]);
#if 0   // NEEDS_PORTING
  trace_iwlwifi_dev_ict_read(trans->dev, trans_pcie->ict_index, read);
#endif  // NEEDS_PORTING

  if (!read) {
    return 0;
  }

  /*
   * Collect all entries up to the first 0, starting from ict_index;
   * note we already read at ict_index.
   */
  uint32_t val = 0;
  do {
    val |= read;
    IWL_DEBUG_ISR(trans, "ICT index %d value 0x%08X\n", trans_pcie->ict_index, read);
    ict_table[trans_pcie->ict_index] = 0;
    trans_pcie->ict_index = ((trans_pcie->ict_index + 1) & (ICT_COUNT - 1));

    read = le32_to_cpu(ict_table[trans_pcie->ict_index]);
#if 0   // NEEDS_PORTING
    trace_iwlwifi_dev_ict_read(trans->dev, trans_pcie->ict_index, read);
#endif  // NEEDS_PORTING
  } while (read);

  /* We should not get this value, just ignore it. */
  if (val == 0xffffffff) {
    val = 0;
  }

  // This is a workaround for a hardware bug. The bug may cause the Rx bit (bit 15 before shifting
  // it to 31) to clear when using interrupt coalescing. Fortunately, bits 18 and 19 stay set when
  // this happens so we use them to decide on the real state of the Rx bit. In order words, bit 15
  // is set if bit 18 or bit 19 are set.
  if (val & 0xC0000) {
    val |= 0x8000;
  }

  return (0xff & val) | ((0xff00 & val) << 16);
}

void iwl_pcie_handle_rfkill_irq(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct isr_statistics* isr_stats = &trans_pcie->isr_stats;
  bool hw_rfkill, prev, report;

  mtx_lock(&trans_pcie->mutex);
  prev = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
  hw_rfkill = iwl_is_rfkill_set(trans);
  if (hw_rfkill) {
    set_bit(STATUS_RFKILL_OPMODE, &trans->status);
    set_bit(STATUS_RFKILL_HW, &trans->status);
  }
  if (trans_pcie->opmode_down) {
    report = hw_rfkill;
  } else {
    report = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
  }

  IWL_WARN(trans, "RF_KILL bit toggled to %s.\n", hw_rfkill ? "disable radio" : "enable radio");

  isr_stats->rfkill++;

  if (prev != report) {
    iwl_trans_pcie_rf_kill(trans, report);
  }
  mtx_unlock(&trans_pcie->mutex);

  if (hw_rfkill) {
    if (test_and_clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status)) {
      IWL_DEBUG_RF_KILL(trans, "Rfkill while SYNC HCMD in flight\n");
    }
    sync_completion_signal(&trans_pcie->wait_command_queue);
  } else {
    clear_bit(STATUS_RFKILL_HW, &trans->status);
    if (trans_pcie->opmode_down) {
      clear_bit(STATUS_RFKILL_OPMODE, &trans->status);
    }
  }
}

int iwl_pcie_irq_handler(void* arg) {
  struct iwl_trans* trans = arg;
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  zx_status_t status;
  while ((status = zx_interrupt_wait(trans_pcie->irq_handle, NULL)) == ZX_OK) {
    iwl_pcie_isr(trans);
  }

  return 0;
}

zx_status_t iwl_pcie_isr(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct isr_statistics* isr_stats = &trans_pcie->isr_stats;
  uint32_t inta = 0;
  uint32_t handled = 0;

  /* Disable (but don't clear!) interrupts here to avoid
   * back-to-back ISRs and sporadic interrupts from our NIC.
   * If we have something to service, the tasklet will re-enable ints.
   * If we *don't* have something, we'll re-enable before leaving here.
   */
  iwl_write32(trans, CSR_INT_MASK, 0x00000000);

  iwl_stats_inc(IWL_STATS_CNT_INTS_FROM_FW);

  mtx_lock(&trans_pcie->irq_lock);

  /* dram interrupt table not set yet,
   * use legacy interrupt.
   */
  if (likely(trans_pcie->use_ict)) {
    inta = iwl_pcie_int_cause_ict(trans);
  } else {
    inta = iwl_pcie_int_cause_non_ict(trans);
  }

  if (iwl_have_debug_level(IWL_DL_ISR)) {
    IWL_DEBUG_ISR(trans, "ISR inta 0x%08x, enabled 0x%08x(sw), enabled(hw) 0x%08x, fh 0x%08x\n",
                  inta, trans_pcie->inta_mask, iwl_read32(trans, CSR_INT_MASK),
                  iwl_read32(trans, CSR_FH_INT_STATUS));
    if (inta & (~trans_pcie->inta_mask))
      IWL_DEBUG_ISR(trans, "We got a masked interrupt (0x%08x)\n", inta & (~trans_pcie->inta_mask));
  }

  inta &= trans_pcie->inta_mask;

  /*
   * Ignore interrupt if there's nothing in NIC to service.
   * This may be due to IRQ shared with another device,
   * or due to sporadic interrupts thrown from our NIC.
   */
  if (unlikely(!inta)) {
    IWL_DEBUG_ISR(trans, "Ignore interrupt, inta == 0\n");
    /*
     * Re-enable interrupts here since we don't
     * have anything to service
     */
    if (test_bit(STATUS_INT_ENABLED, &trans->status)) {
      _iwl_enable_interrupts(trans);
    }
    mtx_unlock(&trans_pcie->irq_lock);
    return ZX_OK;
  }

  if (unlikely(inta == 0xFFFFFFFF || (inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
    /*
     * Hardware disappeared. It might have
     * already raised an interrupt.
     */
    IWL_WARN(trans, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
    mtx_unlock(&trans_pcie->irq_lock);
    goto out;
  }

  /* Ack/clear/reset pending uCode interrupts.
   * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
   */
  /* There is a hardware bug in the interrupt mask function that some
   * interrupts (i.e. CSR_INT_BIT_SCD) can still be generated even if
   * they are disabled in the CSR_INT_MASK register. Furthermore the
   * ICT interrupt handling mechanism has another bug that might cause
   * these unmasked interrupts fail to be detected. We workaround the
   * hardware bugs here by ACKing all the possible interrupts so that
   * interrupt coalescing can still be achieved.
   */
  iwl_write32(trans, CSR_INT, inta | ~trans_pcie->inta_mask);

  if (iwl_have_debug_level(IWL_DL_ISR)) {
    IWL_DEBUG_ISR(trans, "inta 0x%08x, enabled 0x%08x\n", inta, iwl_read32(trans, CSR_INT_MASK));
  }

  mtx_unlock(&trans_pcie->irq_lock);

  /* Now service all interrupt bits discovered above. */
  if (inta & CSR_INT_BIT_HW_ERR) {
    IWL_ERR(trans, "Hardware error detected.  Restarting.\n");

    /* Tell the device to stop sending interrupts */
    iwl_disable_interrupts(trans);

    isr_stats->hw++;
    iwl_pcie_irq_handle_error(trans);

    handled |= CSR_INT_BIT_HW_ERR;

    goto out;
  }

  if (iwl_have_debug_level(IWL_DL_ISR)) {
    /* NIC fires this, but we don't use it, redundant with WAKEUP */
    if (inta & CSR_INT_BIT_SCD) {
      IWL_DEBUG_ISR(trans, "Scheduler finished to transmit the frame/frames.\n");
      isr_stats->sch++;
    }

    /* Alive notification via Rx interrupt will do the real work */
    if (inta & CSR_INT_BIT_ALIVE) {
      IWL_DEBUG_ISR(trans, "Alive interrupt\n");
      isr_stats->alive++;
      if (trans->cfg->gen2) {
        /*
         * We can restock, since firmware configured
         * the RFH
         */
        iwl_pcie_rxmq_restock(trans, trans_pcie->rxq);
      }
    }
  }

  /* Safely ignore these bits for debug checks below */
  inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

  /* HW RF KILL switch toggled */
  if (inta & CSR_INT_BIT_RF_KILL) {
    iwl_pcie_handle_rfkill_irq(trans);
    handled |= CSR_INT_BIT_RF_KILL;
  }

  /* Chip got too hot and stopped itself */
  if (inta & CSR_INT_BIT_CT_KILL) {
    IWL_ERR(trans, "Microcode CT kill error detected.\n");
    isr_stats->ctkill++;
    handled |= CSR_INT_BIT_CT_KILL;
  }

  /* Error detected by uCode */
  if (inta & CSR_INT_BIT_SW_ERR) {
    IWL_ERR(trans,
            "Microcode SW error detected. "
            " Restarting 0x%X.\n",
            inta);
    isr_stats->sw++;
    iwl_pcie_irq_handle_error(trans);
    handled |= CSR_INT_BIT_SW_ERR;
  }

  /* uCode wakes up after power-down sleep */
  if (inta & CSR_INT_BIT_WAKEUP) {
    IWL_DEBUG_ISR(trans, "Wakeup interrupt\n");
    iwl_pcie_rxq_check_wrptr(trans);
    iwl_pcie_txq_check_wrptrs(trans);

    isr_stats->wakeup++;

    handled |= CSR_INT_BIT_WAKEUP;
  }

  /* All uCode command responses, including Tx command responses,
   * Rx "responses" (frame-received notification), and other
   * notifications from uCode come through here*/
  if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX | CSR_INT_BIT_RX_PERIODIC)) {
    IWL_DEBUG_ISR(trans, "Rx interrupt\n");
    if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
      handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
      iwl_write32(trans, CSR_FH_INT_STATUS, CSR_FH_INT_RX_MASK);
    }
    if (inta & CSR_INT_BIT_RX_PERIODIC) {
      handled |= CSR_INT_BIT_RX_PERIODIC;
      iwl_write32(trans, CSR_INT, CSR_INT_BIT_RX_PERIODIC);
    }
    /* Sending RX interrupt require many steps to be done in the
     * the device:
     * 1- write interrupt to current index in ICT table.
     * 2- dma RX frame.
     * 3- update RX shared data to indicate last write index.
     * 4- send interrupt.
     * This could lead to RX race, driver could receive RX interrupt
     * but the shared data changes does not reflect this;
     * periodic interrupt will detect any dangling Rx activity.
     */

    /* Disable periodic interrupt; we use it as just a one-shot. */
    iwl_write8(trans, CSR_INT_PERIODIC_REG, CSR_INT_PERIODIC_DIS);

    /*
     * Enable periodic interrupt in 8 msec only if we received
     * real RX interrupt (instead of just periodic int), to catch
     * any dangling Rx interrupt.  If it was just the periodic
     * interrupt, there was no dangling Rx activity, and no need
     * to extend the periodic interrupt; one-shot is enough.
     */
    if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
      iwl_write8(trans, CSR_INT_PERIODIC_REG, CSR_INT_PERIODIC_ENA);
    }

    isr_stats->rx++;

    iwl_pcie_rx_handle(trans, 0);
  }

  /* This "Tx" DMA channel is used only for loading uCode */
  if (inta & CSR_INT_BIT_FH_TX) {
    iwl_write32(trans, CSR_FH_INT_STATUS, CSR_FH_INT_TX_MASK);
    IWL_DEBUG_ISR(trans, "uCode load interrupt\n");
    isr_stats->tx++;
    handled |= CSR_INT_BIT_FH_TX;
    /* Wake up uCode load routine, now that load is complete */
    trans_pcie->ucode_write_complete = true;
    sync_completion_signal(&trans_pcie->ucode_write_waitq);
  }

  if (inta & ~handled) {
    IWL_ERR(trans, "Unhandled INTA bits 0x%08x\n", inta & ~handled);
    isr_stats->unhandled++;
  }

  if (inta & ~(trans_pcie->inta_mask)) {
    IWL_WARN(trans, "Disabled INTA bits 0x%08x were pending\n", inta & ~trans_pcie->inta_mask);
  }

  mtx_lock(&trans_pcie->irq_lock);
  /* only Re-enable all interrupt if disabled by irq */
  if (test_bit(STATUS_INT_ENABLED, &trans->status)) {
    _iwl_enable_interrupts(trans);
  }
  /* we are loading the firmware, enable FH_TX interrupt only */
  else if (handled & CSR_INT_BIT_FH_TX) {
    iwl_enable_fw_load_int(trans);
  }
  /* Re-enable RF_KILL if it occurred */
  else if (handled & CSR_INT_BIT_RF_KILL) {
    iwl_enable_rfkill_int(trans);
  }
  mtx_unlock(&trans_pcie->irq_lock);

out:
  if (trans_pcie->irq_mode == PCI_INTERRUPT_MODE_LEGACY) {
    iwl_pci_ack_interrupt(trans_pcie->pci);
  }
  return ZX_OK;
}

/******************************************************************************
 *
 * ICT functions
 *
 ******************************************************************************/

/* Free dram table */
void iwl_pcie_free_ict(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  iwl_iobuf_release(trans_pcie->ict_tbl);
  trans_pcie->ict_tbl = NULL;
}

/*
 * Allocate dram shared table, it is an aligned memory block of ICT_SIZE.
 * Also reset all data related to ICT table interrupt.
 */
zx_status_t iwl_pcie_alloc_ict(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  zx_status_t status =
      iwl_iobuf_allocate_contiguous(&trans_pcie->pci_dev->dev, ICT_SIZE, &trans_pcie->ict_tbl);
  if (status != ZX_OK) {
    return status;
  }

  // The device expects the shifted physical address to be written to a 32-bit register.
  zx_paddr_t dma_addr = iwl_iobuf_physical(trans_pcie->ict_tbl);
  if ((dma_addr >> ICT_SHIFT) > UINT32_MAX) {
    return ZX_ERR_INTERNAL;
  }

  return status;
}

/* Device is going up inform it about using ICT interrupt table,
 * also we need to tell the driver to start using ICT interrupt.
 */
void iwl_pcie_reset_ict(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (!trans_pcie->ict_tbl) {
    return;
  }

  mtx_lock(&trans_pcie->irq_lock);
  _iwl_disable_interrupts(trans);

  memset(iwl_iobuf_virtual(trans_pcie->ict_tbl), 0, ICT_SIZE);

  uint32_t val = iwl_iobuf_physical(trans_pcie->ict_tbl) >> ICT_SHIFT;
  val |= CSR_DRAM_INT_TBL_ENABLE | CSR_DRAM_INIT_TBL_WRAP_CHECK | CSR_DRAM_INIT_TBL_WRITE_POINTER;

  IWL_DEBUG_ISR(trans, "CSR_DRAM_INT_TBL_REG =0x%x\n", val);

  iwl_write32(trans, CSR_DRAM_INT_TBL_REG, val);
  trans_pcie->use_ict = true;
  trans_pcie->ict_index = 0;
  iwl_write32(trans, CSR_INT, trans_pcie->inta_mask);
  _iwl_enable_interrupts(trans);
  mtx_unlock(&trans_pcie->irq_lock);
}

/* Device is going down disable ict interrupt usage */
void iwl_pcie_disable_ict(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  mtx_lock(&trans_pcie->irq_lock);
  trans_pcie->use_ict = false;
  mtx_unlock(&trans_pcie->irq_lock);
}

#if 0   // NEEDS_PORTING
irqreturn_t iwl_pcie_msix_isr(int irq, void* data) { return IRQ_WAKE_THREAD; }

irqreturn_t iwl_pcie_irq_msix_handler(int irq, void* dev_id) {
  struct msix_entry* entry = dev_id;
  struct iwl_trans_pcie* trans_pcie = iwl_pcie_get_trans_pcie(entry);
  struct iwl_trans* trans = trans_pcie->trans;
  struct isr_statistics* isr_stats = &trans_pcie->isr_stats;
  uint32_t inta_fh, inta_hw;

  lock_map_acquire(&trans->sync_cmd_lockdep_map);

  mtx_lock(&trans_pcie->irq_lock);
  inta_fh = iwl_read32(trans, CSR_MSIX_FH_INT_CAUSES_AD);
  inta_hw = iwl_read32(trans, CSR_MSIX_HW_INT_CAUSES_AD);
  /*
   * Clear causes registers to avoid being handling the same cause.
   */
  iwl_write32(trans, CSR_MSIX_FH_INT_CAUSES_AD, inta_fh);
  iwl_write32(trans, CSR_MSIX_HW_INT_CAUSES_AD, inta_hw);
  mtx_unlock(&trans_pcie->irq_lock);

  trace_iwlwifi_dev_irq_msix(trans->dev, entry, true, inta_fh, inta_hw);

  if (unlikely(!(inta_fh | inta_hw))) {
    IWL_DEBUG_ISR(trans, "Ignore interrupt, inta == 0\n");
    lock_map_release(&trans->sync_cmd_lockdep_map);
    return IRQ_NONE;
  }

  if (iwl_have_debug_level(IWL_DL_ISR))
    IWL_DEBUG_ISR(trans, "ISR inta_fh 0x%08x, enabled 0x%08x\n", inta_fh,
                  iwl_read32(trans, CSR_MSIX_FH_INT_MASK_AD));

  if ((trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_NON_RX) && inta_fh & MSIX_FH_INT_CAUSES_Q0) {
    local_bh_disable();
    iwl_pcie_rx_handle(trans, 0);
    local_bh_enable();
  }

  if ((trans_pcie->shared_vec_mask & IWL_SHARED_IRQ_FIRST_RSS) && inta_fh & MSIX_FH_INT_CAUSES_Q1) {
    local_bh_disable();
    iwl_pcie_rx_handle(trans, 1);
    local_bh_enable();
  }

  /* This "Tx" DMA channel is used only for loading uCode */
  if (inta_fh & MSIX_FH_INT_CAUSES_D2S_CH0_NUM) {
    IWL_DEBUG_ISR(trans, "uCode load interrupt\n");
    isr_stats->tx++;
    /*
     * Wake up uCode load routine,
     * now that load is complete
     */
    trans_pcie->ucode_write_complete = true;
        sync_condition_signal(&trans_pcie->ucode_write_waitq);
  }

  /* Error detected by uCode */
  if ((inta_fh & MSIX_FH_INT_CAUSES_FH_ERR) || (inta_hw & MSIX_HW_INT_CAUSES_REG_SW_ERR) ||
      (inta_hw & MSIX_HW_INT_CAUSES_REG_SW_ERR_V2)) {
    IWL_ERR(trans, "Microcode SW error detected. Restarting 0x%X.\n", inta_fh);
    isr_stats->sw++;
    iwl_pcie_irq_handle_error(trans);
  }

  /* After checking FH register check HW register */
  if (iwl_have_debug_level(IWL_DL_ISR))
    IWL_DEBUG_ISR(trans, "ISR inta_hw 0x%08x, enabled 0x%08x\n", inta_hw,
                  iwl_read32(trans, CSR_MSIX_HW_INT_MASK_AD));

  /* Alive notification via Rx interrupt will do the real work */
  if (inta_hw & MSIX_HW_INT_CAUSES_REG_ALIVE) {
    IWL_DEBUG_ISR(trans, "Alive interrupt\n");
    isr_stats->alive++;
    if (trans->cfg->gen2) {
      /* We can restock, since firmware configured the RFH */
      iwl_pcie_rxmq_restock(trans, trans_pcie->rxq);
    }
  }

  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560 &&
      inta_hw & MSIX_HW_INT_CAUSES_REG_IPC) {
    /* Reflect IML transfer status */
    int res = iwl_read32(trans, CSR_IML_RESP_ADDR);

    IWL_DEBUG_ISR(trans, "IML transfer status: %d\n", res);
    if (res == IWL_IMAGE_RESP_FAIL) {
      isr_stats->sw++;
      iwl_pcie_irq_handle_error(trans);
    }
  } else if (inta_hw & MSIX_HW_INT_CAUSES_REG_WAKEUP) {
    /* uCode wakes up after power-down sleep */
    IWL_DEBUG_ISR(trans, "Wakeup interrupt\n");
    iwl_pcie_rxq_check_wrptr(trans);
    iwl_pcie_txq_check_wrptrs(trans);

    isr_stats->wakeup++;
  }

  /* Chip got too hot and stopped itself */
  if (inta_hw & MSIX_HW_INT_CAUSES_REG_CT_KILL) {
    IWL_ERR(trans, "Microcode CT kill error detected.\n");
    isr_stats->ctkill++;
  }

  /* HW RF KILL switch toggled */
  if (inta_hw & MSIX_HW_INT_CAUSES_REG_RF_KILL) {
    iwl_pcie_handle_rfkill_irq(trans);
  }

  if (inta_hw & MSIX_HW_INT_CAUSES_REG_HW_ERR) {
    IWL_ERR(trans, "Hardware error detected. Restarting.\n");

    isr_stats->hw++;
    iwl_pcie_irq_handle_error(trans);
  }

  iwl_pcie_clear_irq(trans, entry);

  lock_map_release(&trans->sync_cmd_lockdep_map);
  return IRQ_HANDLED;
}
#endif  // NEEDS_PORTING
