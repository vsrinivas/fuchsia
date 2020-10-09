/******************************************************************************
 *
 * Copyright(c) 2003 - 2015 Intel Corporation. All rights reserved.
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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_INTERNAL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_INTERNAL_H_

#include <lib/async/task.h>
#include <lib/sync/completion.h>
#include <lib/sync/condition.h>
#include <threads.h>
#include <zircon/listnode.h>

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-drv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-fh.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

#ifdef __cplusplus
using std::atomic_int;
#endif  // __cplusplus

/* We need 2 entries for the TX command and header, and another one might
 * be needed for potential data in the SKB's head. The remaining ones can
 * be used for frags.
 */
#define IWL_PCIE_MAX_FRAGS(x) (x->max_tbs - 3)

/*
 * RX related structures and functions
 */
#define RX_NUM_QUEUES 1
#define RX_POST_REQ_ALLOC 2
#define RX_CLAIM_REQ_ALLOC 8
#define RX_PENDING_WATERMARK 16
#define FIRST_RX_QUEUE 512

// The reserved space in TxQ before we can insert more packets.
//
// The number comes from the comment of 'DMA-QUEUE-GENERAL-FUNCTIONS' section in the tx.c.
// The driver needs to keep at least 2 empty entries to distinguish the full/empty state.
//
// (Note that this is how the original code does. It looks too conservative. But there might be
//  some hardware/firmware design consideration.
//
#define TX_RESERVED_SPACE 3

struct iwl_host_cmd;

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

/**
 * struct iwl_rx_mem_buffer
 * @io_buf: driver's handle to the rxb memory
 * @invalid: rxb is in driver ownership - not owned by HW
 * @vid: index of this rxb in the global table
 * @size: size used from the buffer
 */
struct iwl_rx_mem_buffer {
  io_buffer_t io_buf;
  uint16_t vid;
  bool invalid;
  list_node_t list;
  uint32_t size;
};

/**
 * struct isr_statistics - interrupt statistics
 *
 */
struct isr_statistics {
  uint32_t hw;
  uint32_t sw;
  uint32_t err_code;
  uint32_t sch;
  uint32_t alive;
  uint32_t rfkill;
  uint32_t ctkill;
  uint32_t wakeup;
  uint32_t rx;
  uint32_t tx;
  uint32_t unhandled;
};

#define IWL_RX_TD_TYPE_MSK 0xff000000
#define IWL_RX_TD_SIZE_MSK 0x00ffffff
#define IWL_RX_TD_SIZE_2K BIT(11)
#define IWL_RX_TD_TYPE 0

/**
 * struct iwl_rx_transfer_desc - transfer descriptor
 * @type_n_size: buffer type (bit 0: external buff valid,
 *  bit 1: optional footer valid, bit 2-7: reserved)
 *  and buffer size
 * @addr: ptr to free buffer start address
 * @rbid: unique tag of the buffer
 * @reserved: reserved
 */
struct iwl_rx_transfer_desc {
  __le32 type_n_size;
  __le64 addr;
  __le16 rbid;
  __le16 reserved;
} __packed;

#define IWL_RX_CD_SIZE 0xffffff00

/**
 * struct iwl_rx_completion_desc - completion descriptor
 * @type: buffer type (bit 0: external buff valid,
 *  bit 1: optional footer valid, bit 2-7: reserved)
 * @status: status of the completion
 * @reserved1: reserved
 * @rbid: unique tag of the received buffer
 * @size: buffer size, masked by IWL_RX_CD_SIZE
 * @reserved2: reserved
 */
struct iwl_rx_completion_desc {
  uint8_t type;
  uint8_t status;
  __le16 reserved1;
  __le16 rbid;
  __le32 size;
  uint8_t reserved2[22];
} __packed;

/**
 * struct iwl_rxq - Rx queue
 * @id: queue index
 * @descriptors: IO buffer of receive buffer descriptors (rbd).
 *  Address size is 32 bit in pre-9000 devices and 64 bit in 9000 devices.
 *  In 22560 devices it is a pointer to a list of iwl_rx_transfer_desc's
 * @used_bd: driver's pointer to buffer of used receive buffer descriptors (rbd)
 * @used_bd_dma: physical address of buffer of used receive buffer descriptors (rbd)
 * @tr_tail: driver's pointer to the transmission ring tail buffer
 * @tr_tail_dma: physical address of the buffer for the transmission ring tail
 * @cr_tail: driver's pointer to the completion ring tail buffer
 * @cr_tail_dma: physical address of the buffer for the completion ring tail
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @write_actual:
 * @rx_free: list of RBDs with allocated RB ready for use
 * @need_update: flag to indicate we need to update read/write index
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 * @lock:
 * @queue: actual rx queue. Not used for multi-rx queue.
 *
 * NOTE:  rx_free is used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rxq {
  int id;
  io_buffer_t descriptors;

#if 0  // NEEDS_PORTING
  //  These fields are only used for multi-rx queue devices.
  union {
    void* used_bd;
    __le32* bd_32;
    struct iwl_rx_completion_desc* cd;
  };
  dma_addr_t used_bd_dma;

  // These fields are only used for 22560 devices and newer.
  __le16* tr_tail;
  dma_addr_t tr_tail_dma;
  __le16* cr_tail;
  dma_addr_t cr_tail_dma;
#endif  // NEEDS_PORTING

  uint32_t read;
  uint32_t write;
  uint32_t free_count;
  uint32_t write_actual;
  uint32_t queue_size;
  list_node_t rx_free;
  bool need_update;
  io_buffer_t rb_status;
  mtx_t lock;
  struct napi_struct napi;  // TODO(43218): replace with something like mvmvif so that when
                            //              packet is received we know where to dispatch.
  struct iwl_rx_mem_buffer* queue[RX_QUEUE_SIZE];
};

struct iwl_dma_ptr {
  io_buffer_t io_buf;
  dma_addr_t dma;
  void* addr;
  size_t size;
};

/**
 * iwl_queue_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 */
static inline int iwl_queue_inc_wrap(struct iwl_trans* trans, int index) {
  return ++index & (trans->cfg->base_params->max_tfd_queue_size - 1);
}

/**
 * iwl_get_closed_rb_stts - get closed rb stts from different structs
 * @rxq - the rxq to get the rb stts from
 */
static inline __le16 iwl_get_closed_rb_status(struct iwl_trans* trans, struct iwl_rxq* rxq) {
  if (trans->cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
    __le16* rb_status = (__le16*)io_buffer_virt(&rxq->rb_status);

    return READ_ONCE(*rb_status);
  } else {
    struct iwl_rb_status* rb_status = (struct iwl_rb_status*)io_buffer_virt(&rxq->rb_status);

    return READ_ONCE(rb_status->closed_rb_num);
  }
}

/**
 * iwl_queue_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 */
static inline int iwl_queue_dec_wrap(struct iwl_trans* trans, int index) {
  return --index & (trans->cfg->base_params->max_tfd_queue_size - 1);
}

struct iwl_cmd_meta {
  /* only for SYNC commands, iff the reply skb is wanted */
  struct iwl_host_cmd* source;
  uint32_t flags;
  uint32_t tbs;
};

#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

/*
 * The FH will write back to the first TB only, so we need to copy some data
 * into the buffer regardless of whether it should be mapped or not.
 * This indicates how big the first TB must be to include the scratch buffer
 * and the assigned PN.
 * Since PN location is 8 bytes at offset 12, it's 20 now.
 * If we make it bigger then allocations will be bigger and copy slower, so
 * that's probably not useful.
 */
#define IWL_FIRST_TB_SIZE 20
#define IWL_FIRST_TB_SIZE_ALIGN ROUND_UP(IWL_FIRST_TB_SIZE, 64)

struct iwl_pcie_txq_entry {
  io_buffer_t cmd;  // Used to store the command
  io_buffer_t dup_io_buf;
  struct iwl_cmd_meta meta;
};

struct iwl_pcie_first_tb_buf {
  uint8_t buf[IWL_FIRST_TB_SIZE_ALIGN];
};

typedef void(iwlwifi_timer_callback_t)(void* data);

/**
 * struct iwlwifi_timer_info - Timer used for txq stuck timer.
 * @task: async task for scheduling the timer to wake up
 * @dispatcher: async dispatcher to post the task to
 * @finished: notifies the timer that the handler has finished
 * @lock: timer lock
 */
struct iwlwifi_timer_info {
  async_task_t task;
  async_dispatcher_t* dispatcher;
  sync_completion_t finished;
  mtx_t lock;
};

// Initialize the timer.
void iwlwifi_timer_init(struct iwl_trans* trans, struct iwlwifi_timer_info* timer);
// Set the timer to run the handler after |delay|. If the timer is already running, this will reset.
void iwlwifi_timer_set(struct iwlwifi_timer_info* timer, zx_duration_t delay);
// If the timer is running, stop it. If the handler is currently running, wait for it to finish.
void iwlwifi_timer_stop(struct iwlwifi_timer_info* timer);

/**
 * struct iwl_txq - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @tfds: transmit frame descriptors (DMA memory)
 * @first_tb_bufs: start of command headers, including scratch buffers, for
 *  the writeback -- this is DMA memory and an array holding one buffer
 *  for each command on the queue
 * @entries: transmit entries (driver state)
 * @lock: queue lock
 * @stuck_timer: timer that fires if queue gets stuck
 * @trans_pcie: pointer back to transport (for timer)
 * @need_update: indicates need to update read/write index
 * @ampdu: true if this queue is an ampdu queue for an specific RA/TID
 * @wd_timeout: queue watchdog timeout - per queue
 * @frozen: tx stuck queue timer is frozen
 * @frozen_expiry_remainder: remember how long until the timer fires
 * @bc_tbl: byte count table of the queue (relevant only for gen2 transport)
 * @write_ptr: 1-st empty entry (index) host_w
 * @read_ptr: last used entry (index) host_r
 * @dma_addr:  physical addr for BD's
 * @n_window: safe queue window
 * @id: queue id
 * @low_mark: low watermark, resume queue if free space more than this
 * @high_mark: high watermark, stop queue if free space less than this
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 *
 * Note the difference between TFD_QUEUE_SIZE_MAX and n_window: the hardware
 * always assumes 256 descriptors, so TFD_QUEUE_SIZE_MAX is always 256 (unless
 * there might be HW changes in the future). For the normal TX
 * queues, n_window, which is the size of the software queue data
 * is also 256; however, for the command queue, n_window is only
 * 32 since we don't need so many commands pending. Since the HW
 * still uses 256 BDs for DMA though, TFD_QUEUE_SIZE_MAX stays 256.
 * This means that we end up with the following:
 *  HW entries: | 0 | ... | N * 32 | ... | N * 32 + 31 | ... | 255 |
 *  SW entries:           | 0      | ... | 31          |
 * where N is a number between 0 and 7. This means that the SW
 * data is a window overlayed over the HW queue.
 */
struct iwl_txq {
  io_buffer_t tfds;
  io_buffer_t first_tb_bufs;
  struct iwl_pcie_txq_entry* entries;
  mtx_t lock;
  unsigned long frozen_expiry_remainder;
  struct iwlwifi_timer_info stuck_timer;
  struct iwl_trans_pcie* trans_pcie;
  bool need_update;
  bool frozen;
  bool ampdu;
  int block;
  zx_duration_t wd_timeout;
  struct sk_buff_head overflow_q;
  struct iwl_dma_ptr bc_tbl;

  int write_ptr;
  int read_ptr;
  dma_addr_t dma_addr;
  uint16_t n_window;
  uint32_t id;
  int low_mark;
  int high_mark;
};

static inline dma_addr_t iwl_pcie_get_first_tb_dma(struct iwl_txq* txq, int idx) {
  return io_buffer_phys(&txq->first_tb_bufs) + (sizeof(struct iwl_pcie_first_tb_buf) * idx);
}

struct iwl_tso_hdr_page {
  struct page* page;
  uint8_t* pos;
};

#ifdef CPTCFG_IWLWIFI_DEBUGFS
/**
 * enum iwl_fw_mon_dbgfs_state - the different states of the monitor_data
 * debugfs file
 *
 * @IWL_FW_MON_DBGFS_STATE_CLOSED: the file is closed.
 * @IWL_FW_MON_DBGFS_STATE_OPEN: the file is open.
 * @IWL_FW_MON_DBGFS_STATE_DISABLED: the file is disabled, once this state is
 *  set the file can no longer be used.
 */
enum iwl_fw_mon_dbgfs_state {
  IWL_FW_MON_DBGFS_STATE_CLOSED,
  IWL_FW_MON_DBGFS_STATE_OPEN,
  IWL_FW_MON_DBGFS_STATE_DISABLED,
};
#endif

/**
 * enum iwl_shared_irq_flags - level of sharing for irq
 * @IWL_SHARED_IRQ_NON_RX: interrupt vector serves non rx causes.
 * @IWL_SHARED_IRQ_FIRST_RSS: interrupt vector serves first RSS queue.
 */
enum iwl_shared_irq_flags {
  IWL_SHARED_IRQ_NON_RX = BIT(0),
  IWL_SHARED_IRQ_FIRST_RSS = BIT(1),
};

/**
 * enum iwl_image_response_code - image response values
 * @IWL_IMAGE_RESP_DEF: the default value of the register
 * @IWL_IMAGE_RESP_SUCCESS: iml was read successfully
 * @IWL_IMAGE_RESP_FAIL: iml reading failed
 */
enum iwl_image_response_code {
  IWL_IMAGE_RESP_DEF = 0,
  IWL_IMAGE_RESP_SUCCESS = 1,
  IWL_IMAGE_RESP_FAIL = 2,
};

/**
 * struct iwl_self_init_dram - dram data used by self init process
 * @fw: lmac and umac dram data
 * @fw_cnt: total number of items in array
 * @paging: paging dram data
 * @paging_cnt: total number of items in array
 */
struct iwl_self_init_dram {
  struct iwl_dram_data* fw;
  int fw_cnt;
  struct iwl_dram_data* paging;
  int paging_cnt;
};

/**
 * struct cont_rec: continuous recording data structure
 * @prev_wr_ptr: the last address that was read in monitor_data
 *  debugfs file
 * @prev_wrap_cnt: the wrap count that was used during the last read in
 *  monitor_data debugfs file
 * @state: the state of monitor_data debugfs file as described
 *  in &iwl_fw_mon_dbgfs_state enum
 * @mutex: locked while reading from monitor_data debugfs file
 */
#ifdef CPTCFG_IWLWIFI_DEBUGFS
struct cont_rec {
  uint32_t prev_wr_ptr;
  uint32_t prev_wrap_cnt;
  uint8_t state;
  /* Used to sync monitor_data debugfs file with driver unload flow */
  struct mutex mutex;
};
#endif

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_pool: initial pool of iwl_rx_mem_buffer for all the queues
 * @global_table: table mapping received VID from hw to rxb
 * @bti: bus transaction initiator handle
 * @rba: allocator for RX replenishing
 * @ctxt_info: context information for FW self init
 * @ctxt_info_gen3: context information for gen3 devices
 * @prph_info: prph info for self init
 * @prph_scratch: prph scratch for self init
 * @ctxt_info_dma_addr: dma addr of context information
 * @prph_info_dma_addr: dma addr of prph info
 * @prph_scratch_dma_addr: dma addr of prph scratch
 * @ctxt_info_dma_addr: dma addr of context information
 * @init_dram: DRAM data of firmware image (including paging).
 *  Context information addresses will be taken from here.
 *  This is driver's local copy for keeping track of size and
 *  count for allocating and freeing the memory.
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @scd_bc_tbls: pointer to the byte count table of the scheduler
 * @kw: keep warm address
 * @pci_dev: basic pci-network driver stuff
 * @pci: PCI protocol
 * @mmio: PCI memory mapped IO
 * @ucode_write_complete: indicates that the ucode has been copied.
 * @ucode_write_waitq: wait queue for uCode load
 * @cmd_queue - command queue number
 * @def_rx_queue - default rx queue number
 * @rx_buf_size: Rx buffer size
 * @bc_table_dword: true if the BC table expects DWORD (as opposed to bytes)
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @sw_csum_tx: if true, then the transport will compute the csum of the TXed
 *  frame.
 * @reg_lock: protect hw register access
 * @mutex: to protect stop_device / start_fw / start_hw
 * @cmd_in_flight: true when we have a host command in flight
#ifdef CPTCFG_IWLWIFI_DEBUGFS
 * @fw_mon_data: fw continuous recording data
#endif
 * @msix_entries: array of MSI-X entries
 * @msix_enabled: true if managed to enable MSI-X
 * @shared_vec_mask: the type of causes the shared vector handles
 *  (see iwl_shared_irq_flags).
 * @alloc_vecs: the number of interrupt vectors allocated by the OS
 * @def_irq: default irq for non rx causes
 * @fh_init_mask: initial unmasked fh causes
 * @hw_init_mask: initial unmasked hw causes
 * @fh_mask: current unmasked fh causes
 * @hw_mask: current unmasked hw causes
 * @in_rescan: true if we have triggered a device rescan
 */
struct iwl_trans_pcie {
  struct iwl_rxq* rxq;
  struct iwl_rx_mem_buffer rx_pool[RX_POOL_SIZE];
  struct iwl_rx_mem_buffer* global_table[RX_POOL_SIZE];
  zx_handle_t bti;
  union {
    struct iwl_context_info* ctxt_info;
    struct iwl_context_info_gen3* ctxt_info_gen3;
  };
  struct iwl_prph_info* prph_info;
  struct iwl_prph_scratch* prph_scratch;
  dma_addr_t ctxt_info_dma_addr;
  dma_addr_t prph_info_dma_addr;
  dma_addr_t prph_scratch_dma_addr;
  dma_addr_t iml_dma_addr;
  struct iwl_self_init_dram init_dram;
  struct iwl_trans* trans;

#if 0  // NEEDS_PORTING
    struct net_device napi_dev;

    struct __percpu iwl_tso_hdr_page* tso_hdr_page;
#endif  // NEEDS_PORTING

  /* INT ICT Table */
  io_buffer_t ict_tbl;
  int ict_index;
  bool use_ict;
  bool is_down, opmode_down;
  bool debug_rfkill;
  struct isr_statistics isr_stats;

  zx_handle_t irq_handle;
  thrd_t irq_thread;
  mtx_t irq_lock;
  mtx_t mutex;
  uint32_t inta_mask;
  uint32_t scd_base_addr;
  struct iwl_dma_ptr scd_bc_tbls;
  struct iwl_dma_ptr kw;

  struct iwl_txq* txq_memory;
  struct iwl_txq* txq[IWL_MAX_TVQM_QUEUES];
  unsigned long queue_used[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
  unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];

  /* PCI bus related data */
  struct pci_dev* pci_dev;
  pci_protocol_t* pci;
  mmio_buffer_t mmio;

  bool ucode_write_complete;
  sync_completion_t ucode_write_waitq;
  sync_completion_t wait_command_queue;
#if 0  // NEEDS_PORTING
    wait_queue_head_t d0i3_waitq;
#endif  // NEEDS_PORTING

  uint8_t page_offs, dev_cmd_offs;

  uint8_t cmd_queue;
  uint8_t def_rx_queue;
  uint8_t cmd_fifo;
  unsigned int cmd_q_wdg_timeout;
  uint8_t n_no_reclaim_cmds;
  uint8_t no_reclaim_cmds[MAX_NO_RECLAIM_CMDS];
  uint8_t max_tbs;
  uint16_t tfd_size;

  enum iwl_amsdu_size rx_buf_size;
  bool bc_table_dword;
  bool scd_set_active;
  bool sw_csum_tx;
  bool pcie_dbg_dumped_once;

  /*protect hw register */
  mtx_t reg_lock;
  bool cmd_hold_nic_awake;
  bool ref_cmd_in_flight;

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  struct cont_rec fw_mon_data;
#endif

#if 0  // NEEDS_PORTING
    struct msix_entry msix_entries[IWL_MAX_RX_HW_QUEUES];
#endif  // NEEDS_PORTING
  bool msix_enabled;
  uint8_t shared_vec_mask;
  uint32_t alloc_vecs;
  uint32_t def_irq;
  uint32_t fh_init_mask;
  uint32_t hw_init_mask;
  uint32_t fh_mask;
  uint32_t hw_mask;
#if 0  // NEEDS_PORTING
    cpumask_t affinity_mask[IWL_MAX_RX_HW_QUEUES];
#endif  // NEEDS_PORTING
  bool in_rescan;
};

static inline struct iwl_trans_pcie* IWL_TRANS_GET_PCIE_TRANS(struct iwl_trans* trans) {
  return (struct iwl_trans_pcie*)trans->trans_specific;
}

#if 0  // NEEDS_PORTING
static inline void iwl_pcie_clear_irq(struct iwl_trans* trans, struct msix_entry* entry) {
    /*
     * Before sending the interrupt the HW disables it to prevent
     * a nested interrupt. This is done by writing 1 to the corresponding
     * bit in the mask register. After handling the interrupt, it should be
     * re-enabled by clearing this bit. This register is defined as
     * write 1 clear (W1C) register, meaning that it's being clear
     * by writing 1 to the bit.
     */
    iwl_write32(trans, CSR_MSIX_AUTOMASK_ST_AD, BIT(entry->entry));
}
#endif  // NEEDS_PORTING

static inline struct iwl_trans* iwl_trans_pcie_get_trans(struct iwl_trans_pcie* trans_pcie) {
  return containerof(trans_pcie, struct iwl_trans, trans_specific);
}

/**
 * struct iwl_pcie_device - PCI specific data
 * @device_id: PCI device ID.
 * @subsystem_device_id: PCI subsystem device ID.
 * @config: Config for the current device. See iwl-config.h.
 */
struct iwl_pci_device {
  uint16_t device_id;
  uint16_t subsystem_device_id;
  const struct iwl_cfg* config;
};

/*
 * Convention: trans API functions: iwl_trans_pcie_XXX
 *  Other functions: iwl_pcie_XXX
 */
struct iwl_trans* iwl_trans_pcie_alloc(const pci_protocol_t* pci,
                                       const struct iwl_pci_device* device);
void iwl_trans_pcie_free(struct iwl_trans* trans);

/*****************************************************
 * RX
 ******************************************************/
zx_status_t iwl_pcie_rx_init(struct iwl_trans* trans);
int iwl_pcie_gen2_rx_init(struct iwl_trans* trans);
int iwl_pcie_irq_handler(void* arg);
zx_status_t iwl_pcie_isr(struct iwl_trans* trans);
#if 0  // NEEDS_PORTING
irqreturn_t iwl_pcie_msix_isr(int irq, void* data);
irqreturn_t iwl_pcie_irq_msix_handler(int irq, void* dev_id);
irqreturn_t iwl_pcie_irq_rx_msix_handler(int irq, void* dev_id);
#endif  // NEEDS_PORTING
int iwl_pcie_rx_stop(struct iwl_trans* trans);
void iwl_pcie_rx_free(struct iwl_trans* trans);
int iwl_pcie_dummy_napi_poll(struct napi_struct* napi, int budget);

/*****************************************************
 * ICT - interrupt handling
 ******************************************************/
zx_status_t iwl_pcie_alloc_ict(struct iwl_trans* trans);
void iwl_pcie_free_ict(struct iwl_trans* trans);
void iwl_pcie_reset_ict(struct iwl_trans* trans);
void iwl_pcie_disable_ict(struct iwl_trans* trans);

// Exposed for tests only.
uint32_t iwl_pcie_int_cause_ict(struct iwl_trans* trans);

/*****************************************************
 * TX / HCMD
 ******************************************************/
zx_status_t iwl_pcie_tx_init(struct iwl_trans* trans);
int iwl_pcie_gen2_tx_init(struct iwl_trans* trans, int txq_id, int queue_size);
void iwl_pcie_tx_start(struct iwl_trans* trans, uint32_t scd_base_addr);
zx_status_t iwl_pcie_tx_stop(struct iwl_trans* trans);
void iwl_pcie_tx_free(struct iwl_trans* trans);
bool iwl_trans_pcie_txq_enable(struct iwl_trans* trans, int queue, uint16_t ssn,
                               const struct iwl_trans_txq_scd_cfg* cfg, zx_duration_t wdg_timeout);
void iwl_trans_pcie_txq_disable(struct iwl_trans* trans, int queue, bool configure_scd);
void iwl_trans_pcie_txq_set_shared_mode(struct iwl_trans* trans, uint32_t txq_id, bool shared_mode);
void iwl_trans_pcie_log_scd_error(struct iwl_trans* trans, struct iwl_txq* txq);
zx_status_t iwl_trans_pcie_tx(struct iwl_trans* trans, const wlan_tx_packet_t* pkt,
                              const struct iwl_device_cmd* dev_cmd, int txq_id);
void iwl_pcie_txq_check_wrptrs(struct iwl_trans* trans);
zx_status_t iwl_trans_pcie_send_hcmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd);
zx_status_t iwl_pcie_cmdq_reclaim(struct iwl_trans* trans, int txq_id, uint32_t idx);
void iwl_pcie_gen2_txq_inc_wr_ptr(struct iwl_trans* trans, struct iwl_txq* txq);
void iwl_pcie_hcmd_complete(struct iwl_trans* trans, struct iwl_rx_cmd_buffer* rxb);
void iwl_trans_pcie_reclaim(struct iwl_trans* trans, int txq_id, int ssn);
void iwl_trans_pcie_tx_reset(struct iwl_trans* trans);
void iwl_pcie_gen2_update_byte_tbl(struct iwl_trans_pcie* trans_pcie, struct iwl_txq* txq,
                                   uint16_t byte_cnt, int num_tbs);

#if 0  // NEEDS_PORTING
static inline uint16_t iwl_pcie_tfd_tb_get_len(struct iwl_trans* trans, void* _tfd, uint8_t idx) {
    if (trans->cfg->use_tfh) {
        struct iwl_tfh_tfd* tfd = _tfd;
        struct iwl_tfh_tb* tb = &tfd->tbs[idx];

        return le16_to_cpu(tb->tb_len);
    } else {
        struct iwl_tfd* tfd = _tfd;
        struct iwl_tfd_tb* tb = &tfd->tbs[idx];

        return le16_to_cpu(tb->hi_n_len) >> 4;
    }
}

/*****************************************************
 * Error handling
 ******************************************************/
void iwl_pcie_dump_csr(struct iwl_trans* trans);

/*****************************************************
 * Helpers
 ******************************************************/
#endif  // NEEDS_PORTING
static inline void _iwl_disable_interrupts(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  clear_bit(STATUS_INT_ENABLED, &trans->status);
  if (!trans_pcie->msix_enabled) {
    /* disable interrupts from uCode/NIC to host */
    iwl_write32(trans, CSR_INT_MASK, 0x00000000);

    /* acknowledge/clear/reset any interrupts still pending
     * from uCode or flow handler (Rx/Tx DMA) */
    iwl_write32(trans, CSR_INT, 0xffffffff);
    iwl_write32(trans, CSR_FH_INT_STATUS, 0xffffffff);
  } else {
    /* disable all the interrupt we might use */
    iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, trans_pcie->fh_init_mask);
    iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD, trans_pcie->hw_init_mask);
  }
  IWL_DEBUG_ISR(trans, "Disabled interrupts\n");
}
#if 0  // NEEDS_PORTING

#define IWL_NUM_OF_COMPLETION_RINGS 31
#define IWL_NUM_OF_TRANSFER_RINGS 527

static inline int iwl_pcie_get_num_sections(const struct fw_img* fw, int start) {
    int i = 0;

    while (start < fw->num_sec && fw->sec[start].offset != CPU1_CPU2_SEPARATOR_SECTION &&
            fw->sec[start].offset != PAGING_SEPARATOR_SECTION) {
        start++;
        i++;
    }

    return i;
}

static inline int iwl_pcie_ctxt_info_alloc_dma(struct iwl_trans* trans, const struct fw_desc* sec,
        struct iwl_dram_data* dram) {
    dram->block = dma_alloc_coherent(trans->dev, sec->len, &dram->physical, GFP_KERNEL);
    if (!dram->block) {
        return -ENOMEM;
    }

    dram->size = sec->len;
    memcpy(dram->block, sec->data, sec->len);

    return 0;
}

static inline void iwl_pcie_ctxt_info_free_fw_img(struct iwl_trans* trans) {
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    struct iwl_self_init_dram* dram = &trans_pcie->init_dram;
    int i;

    if (!dram->fw) {
        WARN_ON(dram->fw_cnt);
        return;
    }

    for (i = 0; i < dram->fw_cnt; i++) {
        dma_free_coherent(trans->dev, dram->fw[i].size, dram->fw[i].block, dram->fw[i].physical);
    }

    kfree(dram->fw);
    dram->fw_cnt = 0;
    dram->fw = NULL;
}
#endif  // NEEDS_PORTING

static inline void iwl_disable_interrupts(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  mtx_lock(&trans_pcie->irq_lock);
  _iwl_disable_interrupts(trans);
  mtx_unlock(&trans_pcie->irq_lock);
}

static inline void _iwl_enable_interrupts(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  IWL_DEBUG_ISR(trans, "Enabling interrupts\n");
  set_bit(STATUS_INT_ENABLED, &trans->status);
  if (!trans_pcie->msix_enabled) {
    trans_pcie->inta_mask = CSR_INI_SET_MASK;
    iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
  } else {
    /*
     * fh/hw_mask keeps all the unmasked causes.
     * Unlike msi, in msix cause is enabled when it is unset.
     */
    trans_pcie->hw_mask = trans_pcie->hw_init_mask;
    trans_pcie->fh_mask = trans_pcie->fh_init_mask;
    iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, ~trans_pcie->fh_mask);
    iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD, ~trans_pcie->hw_mask);
  }
}

static inline void iwl_enable_interrupts(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  mtx_lock(&trans_pcie->irq_lock);
  _iwl_enable_interrupts(trans);
  mtx_unlock(&trans_pcie->irq_lock);
}

static inline void iwl_enable_hw_int_msk_msix(struct iwl_trans* trans, uint32_t msk) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD, ~msk);
  trans_pcie->hw_mask = msk;
}

static inline void iwl_enable_fh_int_msk_msix(struct iwl_trans* trans, uint32_t msk) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, ~msk);
  trans_pcie->fh_mask = msk;
}

static inline void iwl_enable_fw_load_int(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  IWL_DEBUG_ISR(trans, "Enabling FW load interrupt\n");
  if (!trans_pcie->msix_enabled) {
    trans_pcie->inta_mask = CSR_INT_BIT_FH_TX;
    iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
  } else {
    iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD, trans_pcie->hw_init_mask);
    iwl_enable_fh_int_msk_msix(trans, MSIX_FH_INT_CAUSES_D2S_CH0_NUM);
  }
}

static inline uint16_t iwl_pcie_get_cmd_index(const struct iwl_txq* q, uint32_t index) {
  return index & (q->n_window - 1);
}

static inline void* iwl_pcie_get_tfd(struct iwl_trans* trans, struct iwl_txq* txq, int idx) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (trans->cfg->use_tfh) {
    idx = iwl_pcie_get_cmd_index(txq, idx);
  }

  char* ptr = (char*)io_buffer_virt(&txq->tfds);
  return ptr + trans_pcie->tfd_size * idx;
}

#if 0  // NEEDS_PORTING
static inline const char* queue_name(struct device* dev, struct iwl_trans_pcie* trans_p, int i) {
    if (trans_p->shared_vec_mask) {
        int vec = trans_p->shared_vec_mask & IWL_SHARED_IRQ_FIRST_RSS ? 1 : 0;

        if (i == 0) {
            return DRV_NAME ": shared IRQ";
        }

        return devm_kasprintf(dev, GFP_KERNEL, DRV_NAME ": queue %d", i + vec);
    }
    if (i == 0) {
        return DRV_NAME ": default queue";
    }

    if (i == trans_p->alloc_vecs - 1) {
        return DRV_NAME ": exception";
    }

    return devm_kasprintf(dev, GFP_KERNEL, DRV_NAME ": queue %d", i);
}
#endif  // NEEDS_PORTING

static inline void iwl_enable_rfkill_int(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  IWL_DEBUG_ISR(trans, "Enabling rfkill interrupt\n");
  if (!trans_pcie->msix_enabled) {
    trans_pcie->inta_mask = CSR_INT_BIT_RF_KILL;
    iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
  } else {
    iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, trans_pcie->fh_init_mask);
    iwl_enable_hw_int_msk_msix(trans, MSIX_HW_INT_CAUSES_REG_RF_KILL);
  }

  if (trans->cfg->device_family == IWL_DEVICE_FAMILY_9000) {
    /*
     * On 9000-series devices this bit isn't enabled by default, so
     * when we power down the device we need set the bit to allow it
     * to wake up the PCI-E bus for RF-kill interrupts.
     */
    iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
  }
}

void iwl_pcie_handle_rfkill_irq(struct iwl_trans* trans);

// The hardware queue is available now, start queueing packet to hardware.  iwl_mvm_mac_itxq_xmit()
// is called then.
static inline void iwl_wake_queue(struct iwl_trans* trans, struct iwl_txq* txq) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (test_and_clear_bit(txq->id, trans_pcie->queue_stopped)) {
    IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d\n", txq->id);
    iwl_op_mode_queue_not_full(trans->op_mode, txq->id);
  }
}

// Hardware Tx queue is full, stop queueing packets from MLME.
static inline void iwl_stop_queue(struct iwl_trans* trans, struct iwl_txq* txq) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (!test_and_set_bit(txq->id, trans_pcie->queue_stopped)) {
    iwl_op_mode_queue_full(trans->op_mode, txq->id);
    IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d\n", txq->id);
  } else {
    IWL_DEBUG_TX_QUEUES(trans, "hwq %d already stopped\n", txq->id);
  }
}

static inline bool iwl_queue_used(const struct iwl_txq* q, int i) {
  int index = iwl_pcie_get_cmd_index(q, i);
  int r = iwl_pcie_get_cmd_index(q, q->read_ptr);
  int w = iwl_pcie_get_cmd_index(q, q->write_ptr);

  return w >= r ? (index >= r && index < w) : !(index < r && index >= w);
}

static inline bool iwl_is_rfkill_set(struct iwl_trans* trans) {
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

  if (trans_pcie->debug_rfkill) {
    return true;
  }

  return !(iwl_read32(trans, CSR_GP_CNTRL) & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
}

static inline void __iwl_trans_pcie_set_bits_mask(struct iwl_trans* trans, uint32_t reg,
                                                  uint32_t mask, uint32_t value) {
  uint32_t v;

#ifdef CPTCFG_IWLWIFI_DEBUG
  WARN_ON_ONCE(value & ~mask);
#endif

  v = iwl_read32(trans, reg);
  v &= ~mask;
  v |= value;
  iwl_write32(trans, reg, v);
}

static inline void __iwl_trans_pcie_clear_bit(struct iwl_trans* trans, uint32_t reg,
                                              uint32_t mask) {
  __iwl_trans_pcie_set_bits_mask(trans, reg, mask, 0);
}

static inline void __iwl_trans_pcie_set_bit(struct iwl_trans* trans, uint32_t reg, uint32_t mask) {
  __iwl_trans_pcie_set_bits_mask(trans, reg, mask, mask);
}

static inline bool iwl_pcie_dbg_on(struct iwl_trans* trans) {
  return (trans->dbg_dest_tlv || trans->ini_valid);
}

void iwl_trans_pcie_rf_kill(struct iwl_trans* trans, bool state);
void iwl_trans_pcie_dump_regs(struct iwl_trans* trans);

#ifdef CPTCFG_IWLWIFI_DEBUGFS
int iwl_trans_pcie_dbgfs_register(struct iwl_trans* trans);
#else
static inline int iwl_trans_pcie_dbgfs_register(struct iwl_trans* trans) { return 0; }
#endif
#if 0  // NEEDS_PORTING

int iwl_pci_fw_exit_d0i3(struct iwl_trans* trans);
int iwl_pci_fw_enter_d0i3(struct iwl_trans* trans);

void iwl_pcie_rx_allocator_work(struct work_struct* data);
#endif  // NEEDS_PORTING

/* common functions that are used by gen2 transport */
int iwl_pcie_gen2_apm_init(struct iwl_trans* trans);
void iwl_pcie_apm_config(struct iwl_trans* trans);
int iwl_pcie_prepare_card_hw(struct iwl_trans* trans);
void iwl_pcie_synchronize_irqs(struct iwl_trans* trans);
bool iwl_pcie_check_hw_rf_kill(struct iwl_trans* trans);
void iwl_trans_pcie_handle_stop_rfkill(struct iwl_trans* trans, bool was_in_rfkill);
void iwl_pcie_txq_free_tfd(struct iwl_trans* trans, struct iwl_txq* txq);
int iwl_queue_space(struct iwl_trans* trans, const struct iwl_txq* q);
void iwl_pcie_apm_stop_master(struct iwl_trans* trans);
void iwl_pcie_conf_msix_hw(struct iwl_trans_pcie* trans_pcie);
zx_status_t iwl_pcie_txq_init(struct iwl_trans* trans, struct iwl_txq* txq, uint16_t slots_num,
                              bool cmd_queue);
zx_status_t iwl_pcie_txq_alloc(struct iwl_trans* trans, struct iwl_txq* txq, uint16_t slots_num,
                               bool cmd_queue);
zx_status_t iwl_pcie_alloc_dma_ptr(struct iwl_trans* trans, struct iwl_dma_ptr* ptr, size_t size);
void iwl_pcie_free_dma_ptr(struct iwl_trans* trans, struct iwl_dma_ptr* ptr);
int iwl_trans_pcie_power_device_off(struct iwl_trans_pcie* trans_pcie);
void iwl_pcie_apply_destination(struct iwl_trans* trans);
void iwl_pcie_free_tso_page(struct iwl_trans_pcie* trans_pcie, struct sk_buff* skb);
void iwl_pcie_txq_unmap(struct iwl_trans* trans, int txq_id);  // for testing
#ifdef CONFIG_INET
struct iwl_tso_hdr_page* get_page_hdr(struct iwl_trans* trans, size_t len);
#endif

/* common functions that are used by gen3 transport */
void iwl_pcie_alloc_fw_monitor(struct iwl_trans* trans, uint8_t max_power);

#if 0  // NEEDS_PORTING
/* transport gen 2 exported functions */
int iwl_trans_pcie_gen2_start_fw(struct iwl_trans* trans, const struct fw_img* fw,
                                 bool run_in_rfkill);
void iwl_trans_pcie_gen2_fw_alive(struct iwl_trans* trans, uint32_t scd_addr);
void iwl_pcie_gen2_txq_free_memory(struct iwl_trans* trans, struct iwl_txq* txq);
int iwl_trans_pcie_dyn_txq_alloc_dma(struct iwl_trans* trans, struct iwl_txq** intxq, int size,
                                     unsigned int timeout);
int iwl_trans_pcie_txq_alloc_response(struct iwl_trans* trans, struct iwl_txq* txq,
                                      struct iwl_host_cmd* hcmd);
int iwl_trans_pcie_dyn_txq_alloc(struct iwl_trans* trans, __le16 flags, uint8_t sta_id, uint8_t tid,
                                 int cmd_id, int size, unsigned int timeout);
void iwl_trans_pcie_dyn_txq_free(struct iwl_trans* trans, int queue);
int iwl_trans_pcie_gen2_tx(struct iwl_trans* trans, struct sk_buff* skb,
                           struct iwl_device_cmd* dev_cmd, int txq_id);
int iwl_trans_pcie_gen2_send_hcmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd);
void iwl_trans_pcie_gen2_stop_device(struct iwl_trans* trans, bool low_power);
void _iwl_trans_pcie_gen2_stop_device(struct iwl_trans* trans, bool low_power);
void iwl_pcie_gen2_txq_unmap(struct iwl_trans* trans, int txq_id);
void iwl_pcie_gen2_tx_free(struct iwl_trans* trans);
void iwl_pcie_gen2_tx_stop(struct iwl_trans* trans);
#endif  // NEEDS_PORTING

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PCIE_INTERNAL_H_
