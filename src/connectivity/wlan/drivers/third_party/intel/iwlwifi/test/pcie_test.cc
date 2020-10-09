/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fake-bti/bti.h>
#include <lib/mock-function/mock-function.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/bti.h>
#include <stdio.h>
#include <zircon/listnode.h>

#include <array>
#include <thread>

#include <ddk/io-buffer.h>
#include <zxtest/zxtest.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
}

namespace {

class PcieTest;

struct iwl_trans_pcie_wrapper {
  struct iwl_trans_pcie trans_pcie;
  PcieTest* test;
};

// In the SyncHostCommandEmpty() test, this function will generate an ECHO response in order to
// simulate the firmware event for iwl_pcie_hcmd_complete().
static void FakeEchoWrite32(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
  if (ofs != HBUS_TARG_WRPTR) {
    return;
  }

  io_buffer_t io_buf;
  zx::bti fake_bti;
  fake_bti_create(fake_bti.reset_and_get_address());
  io_buffer_init(&io_buf, fake_bti.get(), 128, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  struct iwl_rx_cmd_buffer rxcb = {
      ._io_buf = io_buf,
      ._offset = 0,
  };
  struct iwl_rx_packet* resp_pkt = reinterpret_cast<struct iwl_rx_packet*>(io_buffer_virt(&io_buf));
  resp_pkt->len_n_flags = cpu_to_le32(0);
  resp_pkt->hdr.cmd = ECHO_CMD;
  resp_pkt->hdr.group_id = 0;

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];
  resp_pkt->hdr.sequence =
      cpu_to_le16(QUEUE_TO_SEQ(trans_pcie->cmd_queue) | INDEX_TO_SEQ(txq->read_ptr));

  // iwl_pcie_hcmd_complete() will require the txq->lock. However, we already have done it in
  // iwl_trans_pcie_send_hcmd(). So release the lock before calling it. Note that this is safe
  // because in the test, it is always single thread and has no race.
  //
  // Also, iwl_pcie_enqueue_hcmd() holds the reg_lock to keep the write_ptr and cmd_in_flight
  // state consistent. However, there is also a code path via iwl_pcie_hcmd_complete() that
  // requires to hold the reg_lock which will cause a lockup if we do not unlock here.
  //
  // The GCC pragma is to depress the compile warning on mutex check.
  //
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wthread-safety-analysis"
  mtx_unlock(&trans_pcie->reg_lock);
  mtx_unlock(&txq->lock);
  iwl_pcie_hcmd_complete(trans, &rxcb);
  mtx_lock(&txq->lock);
  mtx_lock(&trans_pcie->reg_lock);

  io_buffer_release(&io_buf);
}
#pragma GCC diagnostic pop

class PcieTest : public zxtest::Test {
 public:
  PcieTest() {
    trans_ops_.write8 = write8_wrapper;
    trans_ops_.write32 = write32_wrapper;
    trans_ops_.read32 = read32_wrapper;
    trans_ops_.read_prph = read_prph_wrapper;
    trans_ops_.write_prph = write_prph_wrapper;
    trans_ops_.read_mem = read_mem_wrapper;
    trans_ops_.write_mem = write_mem_wrapper;
    trans_ops_.grab_nic_access = grab_nic_access_wrapper;
    trans_ops_.release_nic_access = release_nic_access_wrapper;
    trans_ops_.ref = ref_wrapper;
    trans_ops_.unref = unref_wrapper;

    cfg_.base_params = &base_params_;
    trans_ = iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &cfg_, &trans_ops_);
    ASSERT_NE(trans_, nullptr);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
    fake_bti_create(&trans_pcie_->bti);

    // Setup the op_mode and its ops. Note that we re-define the 'op_mode_specific' filed to pass
    // the test object reference into the mock function.
    op_mode_ops_.rx = rx_wrapper;
    op_mode_ops_.queue_full = queue_full_wrapper;
    op_mode_ops_.queue_not_full = queue_not_full_wrapper;
    op_mode_.ops = &op_mode_ops_;
    op_mode_.op_mode_specific = this;
    trans_->op_mode = &op_mode_;
  }

  ~PcieTest() override {
    zx_handle_close(trans_pcie_->bti);
    iwl_trans_free(trans_);
  }

  mock_function::MockFunction<void, uint32_t, uint8_t> mock_write8_;
  static void write8_wrapper(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write8_.HasExpectations()) {
      wrapper->test->mock_write8_.Call(ofs, val);
    }
  }

  mock_function::MockFunction<void, uint32_t, uint32_t> mock_write32_;
  static void write32_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write32_.HasExpectations()) {
      wrapper->test->mock_write32_.Call(ofs, val);
    }
  }

  mock_function::MockFunction<uint32_t, uint32_t> mock_read32_;
  static uint32_t read32_wrapper(struct iwl_trans* trans, uint32_t ofs) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_read32_.HasExpectations()) {
      return wrapper->test->mock_read32_.Call(ofs);
    } else {
      return 0;
    }
  }

  mock_function::MockFunction<uint32_t, uint32_t> mock_read_prph_;
  static uint32_t read_prph_wrapper(struct iwl_trans* trans, uint32_t ofs) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_read_prph_.HasExpectations()) {
      return wrapper->test->mock_read_prph_.Call(ofs);
    } else {
      return 0;
    }
  }

  mock_function::MockFunction<void, uint32_t, uint32_t> mock_write_prph_;
  static void write_prph_wrapper(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write_prph_.HasExpectations()) {
      wrapper->test->mock_write_prph_.Call(ofs, val);
    }
  }

  mock_function::MockFunction<zx_status_t, uint32_t, void*, int> mock_read_mem_;
  static zx_status_t read_mem_wrapper(struct iwl_trans* trans, uint32_t addr, void* buf,
                                      int dwords) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_read_mem_.HasExpectations()) {
      return wrapper->test->mock_read_mem_.Call(addr, buf, dwords);
    } else {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  mock_function::MockFunction<zx_status_t, uint32_t, const void*, int> mock_write_mem_;
  static zx_status_t write_mem_wrapper(struct iwl_trans* trans, uint32_t addr, const void* buf,
                                       int dwords) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_write_mem_.HasExpectations()) {
      return wrapper->test->mock_write_mem_.Call(addr, buf, dwords);
    } else {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  mock_function::MockFunction<bool, unsigned long*> mock_grab_nic_access_;
  static bool grab_nic_access_wrapper(struct iwl_trans* trans, unsigned long* flags) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_grab_nic_access_.HasExpectations()) {
      return wrapper->test->mock_grab_nic_access_.Call(flags);
    } else {
      return true;
    }
  }

  mock_function::MockFunction<void, unsigned long*> release_nic_access_;
  static void release_nic_access_wrapper(struct iwl_trans* trans, unsigned long* flags) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->release_nic_access_.HasExpectations()) {
      wrapper->test->release_nic_access_.Call(flags);
    }
  }

  mock_function::MockFunction<void> ref_;
  static void ref_wrapper(struct iwl_trans* trans) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->ref_.HasExpectations()) {
      wrapper->test->ref_.Call();
    }
  }

  mock_function::MockFunction<void> unref_;
  static void unref_wrapper(struct iwl_trans* trans) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->unref_.HasExpectations()) {
      wrapper->test->unref_.Call();
    }
  }

  mock_function::MockFunction<void, struct napi_struct*, struct iwl_rx_cmd_buffer*> op_mode_rx_;
  static void rx_wrapper(struct iwl_op_mode* op_mode, struct napi_struct* napi,
                         struct iwl_rx_cmd_buffer* rxb) {
    auto test = reinterpret_cast<PcieTest*>(op_mode->op_mode_specific);
    if (test->op_mode_rx_.HasExpectations()) {
      test->op_mode_rx_.Call(napi, rxb);
    }
  }

  mock_function::MockFunction<void, int> op_mode_queue_full_;
  static void queue_full_wrapper(struct iwl_op_mode* op_mode, int queue) {
    auto test = reinterpret_cast<PcieTest*>(op_mode->op_mode_specific);
    if (test->op_mode_queue_full_.HasExpectations()) {
      test->op_mode_queue_full_.Call(queue);
    }
  }

  mock_function::MockFunction<void, int> op_mode_queue_not_full_;
  static void queue_not_full_wrapper(struct iwl_op_mode* op_mode, int queue) {
    auto test = reinterpret_cast<PcieTest*>(op_mode->op_mode_specific);
    if (test->op_mode_queue_not_full_.HasExpectations()) {
      test->op_mode_queue_not_full_.Call(queue);
    }
  }

  // Helper function to mark uCode-originated packets. uCode-originated means the packet is from
  // firmware (either a packet from air or a notification from firmware), rather than from driver.
  // Then this packet will NOT be reclaimed by driver (because there is nothing to reclaim).
  //
  // Args:
  //   from, to: the index in the command queue rxb. inclusive.
  //
  void markUcodeOrignated(uint32_t from, uint32_t to) {
    struct iwl_rxq* rxq = &trans_pcie_->rxq[0];  // the command queue
    for (uint32_t i = from; i <= to; ++i) {
      struct iwl_rx_mem_buffer* rxb = rxq->queue[i];
      for (size_t offset = 0; offset < io_buffer_size(&rxb->io_buf, 0);
           offset += ZX_ALIGN(1, FH_RSCSR_FRAME_ALIGN)) {  // move to next packet
        struct iwl_rx_cmd_buffer rxcb = {
            ._io_buf = rxb->io_buf,
            ._offset = static_cast<int>(offset),
        };
        struct iwl_rx_packet* pkt = (struct iwl_rx_packet*)rxb_addr(&rxcb);
        pkt->hdr.sequence |= SEQ_RX_FRAME;
      }
    }
  }

 protected:
  struct iwl_trans* trans_;
  struct iwl_trans_pcie* trans_pcie_;
  struct iwl_base_params base_params_;
  struct iwl_cfg cfg_;
  struct iwl_trans_ops trans_ops_;
  struct iwl_op_mode op_mode_;
  struct iwl_op_mode_ops op_mode_ops_;
};

TEST_F(PcieTest, DisableInterrupts) {
  mock_write32_.ExpectCall(CSR_INT_MASK, 0x00000000);
  mock_write32_.ExpectCall(CSR_INT, 0xffffffff);
  mock_write32_.ExpectCall(CSR_FH_INT_STATUS, 0xffffffff);

  trans_pcie_->msix_enabled = false;
  set_bit(STATUS_INT_ENABLED, &trans_->status);
  iwl_disable_interrupts(trans_);

  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
  mock_write32_.VerifyAndClear();
}

TEST_F(PcieTest, DisableInterruptsMsix) {
  mock_write32_.ExpectCall(CSR_MSIX_FH_INT_MASK_AD, 0xabab);
  mock_write32_.ExpectCall(CSR_MSIX_HW_INT_MASK_AD, 0x4242);

  trans_pcie_->msix_enabled = true;
  trans_pcie_->fh_init_mask = 0xabab;
  trans_pcie_->hw_init_mask = 0x4242;
  set_bit(STATUS_INT_ENABLED, &trans_->status);
  iwl_disable_interrupts(trans_);

  EXPECT_EQ(test_bit(STATUS_INT_ENABLED, &trans_->status), 0);
  mock_write32_.VerifyAndClear();
}

TEST_F(PcieTest, RxInit) {
  trans_->num_rx_queues = 1;
  trans_pcie_->rx_buf_size = IWL_AMSDU_2K;
  ASSERT_OK(iwl_pcie_rx_init(trans_));

  EXPECT_EQ(trans_pcie_->rxq->read, 0);
  EXPECT_EQ(trans_pcie_->rxq->write, 0);
  EXPECT_EQ(trans_pcie_->rxq->free_count, RX_QUEUE_SIZE);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 0);
  EXPECT_EQ(trans_pcie_->rxq->queue_size, RX_QUEUE_SIZE);
  EXPECT_GE(list_length(&trans_pcie_->rxq->rx_free), RX_QUEUE_SIZE);
  EXPECT_TRUE(io_buffer_is_valid(&trans_pcie_->rxq->rb_status));

  struct iwl_rx_mem_buffer* rxb;
  list_for_every_entry (&trans_pcie_->rxq->rx_free, rxb, struct iwl_rx_mem_buffer, list) {
    EXPECT_TRUE(io_buffer_is_valid(&rxb->io_buf));
    EXPECT_EQ(io_buffer_size(&rxb->io_buf, 0), 2 * 1024);
  }

  iwl_pcie_rx_free(trans_);
}

TEST_F(PcieTest, IctTable) {
  ASSERT_OK(iwl_pcie_alloc_ict(trans_));

  // Check the initial state.
  ASSERT_TRUE(io_buffer_is_valid(&trans_pcie_->ict_tbl));
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0);
  EXPECT_EQ(trans_pcie_->ict_index, 0);

  // Reads an interrupt from the table.
  uint32_t* ict_table = static_cast<uint32_t*>(io_buffer_virt(&trans_pcie_->ict_tbl));
  trans_pcie_->ict_index = 0;
  ict_table[0] = 0x1234;
  ict_table[1] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0x12000034);
  EXPECT_EQ(trans_pcie_->ict_index, 1);

  // Reads multiple interrupts from the table.
  trans_pcie_->ict_index = 1;
  ict_table[1] = 1 << 0;
  ict_table[2] = 1 << 1;
  ict_table[3] = 1 << 2;
  ict_table[4] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), (1 << 0) | (1 << 1) | (1 << 2));
  EXPECT_EQ(trans_pcie_->ict_index, 4);

  // This should match ICT_COUNT defined in pcie/rx.c.
  size_t ict_count = io_buffer_size(&trans_pcie_->ict_tbl, 0) / sizeof(uint32_t);

  // Guarantee that we have enough room in the table for the tests.
  ASSERT_GT(ict_count, 42);

  // Correctly wraps the index.
  trans_pcie_->ict_index = ict_count - 1;
  ict_table[ict_count - 1] = 1 << 0;
  ict_table[0] = 1 << 1;
  ict_table[1] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), (1 << 0) | (1 << 1));
  EXPECT_EQ(trans_pcie_->ict_index, 1);

  // Hardware bug workaround.
  trans_pcie_->ict_index = 1;
  ict_table[1] = 0xC0000;
  ict_table[2] = 0;
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0x80000000);

  // Correctly resets
  trans_pcie_->ict_index = 42;
  ict_table[42] = 0xdeadbeef;
  iwl_pcie_reset_ict(trans_);
  EXPECT_EQ(trans_pcie_->ict_index, 0);
  EXPECT_EQ(ict_table[42], 0);

  iwl_pcie_free_ict(trans_);
}

TEST_F(PcieTest, RxInterrupts) {
  trans_->num_rx_queues = 1;
  trans_pcie_->rx_buf_size = IWL_AMSDU_2K;
  trans_pcie_->use_ict = true;
  trans_pcie_->inta_mask = CSR_INI_SET_MASK;
  set_bit(STATUS_DEVICE_ENABLED, &trans_->status);
  ASSERT_OK(iwl_pcie_alloc_ict(trans_));
  ASSERT_OK(iwl_pcie_rx_init(trans_));

  // Allocate Tx buffer since Rx code would use 'txq'.
  base_params_.num_of_queues = 31;
  base_params_.max_tfd_queue_size = 256;
  trans_pcie_->tfd_size = sizeof(struct iwl_tfh_tfd);
  // This need not be started, just created so that iwl_pcie_tx_free() has a target to cancel.
  ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &trans_->loop));
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  ASSERT_TRUE(io_buffer_is_valid(&trans_pcie_->ict_tbl));
  uint32_t* ict_table = static_cast<uint32_t*>(io_buffer_virt(&trans_pcie_->ict_tbl));

  // Spurious interrupt.
  trans_pcie_->ict_index = 0;
  ict_table[0] = 0;
  ASSERT_OK(iwl_pcie_isr(trans_));

  // Set up the ICT table with an RX interrupt at index 0.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // This struct is controlled by the device, closed_rb_num is the read index for the shared ring
  // buffer. For the tests we manually increment the index to push forward the index.
  struct iwl_rb_status* rb_status =
      static_cast<struct iwl_rb_status*>(io_buffer_virt(&trans_pcie_->rxq->rb_status));

  // Process 128 buffers. The driver should process each buffer and indicate this to the hardware by
  // setting the write index (rxq->write_actual).
  rb_status->closed_rb_num = 128;
  markUcodeOrignated(0, 127);
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 127);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 120);

  // Reset the interrupt table.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // Process another 300 buffers, wrapping to the beginning of the circular buffer.
  rb_status->closed_rb_num = 172;
  markUcodeOrignated(128, 171);
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 171);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 168);

  iwl_pcie_rx_free(trans_);
  iwl_pcie_tx_free(trans_);
  free(trans_->loop);
}

class TxTest : public PcieTest {
  void SetUp() {
    base_params_.num_of_queues = 31;
    base_params_.max_tfd_queue_size = 256;
    trans_pcie_->tfd_size = sizeof(struct iwl_tfh_tfd);

    ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &trans_->loop));
    ASSERT_OK(async_loop_start_thread(trans_->loop, "iwlwifi-test-worker", NULL));
  }

  void TearDown() {
    async_loop_quit(trans_->loop);
    async_loop_join_threads(trans_->loop);
    iwl_pcie_tx_free(trans_);
    free(trans_->loop);
  }

 protected:
  // The following class member variables will be set up after this call.
  //
  //  * txq_id_
  //  * txq_
  //
  void SetupTxQueue() {
    ASSERT_OK(iwl_pcie_tx_init(trans_));

    // Setup the queue
    txq_id_ = IWL_MVM_DQA_MIN_DATA_QUEUE;
    iwl_trans_txq_scd_cfg scd_cfg = {};
    ASSERT_FALSE(
        iwl_trans_pcie_txq_enable(trans_, txq_id_, /*ssn*/ 0, &scd_cfg, /*wdg_timeout*/ 0));
    struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
    txq_ = trans_pcie->txq[txq_id_];

    ASSERT_EQ(txq_->read_ptr, 0);
    ASSERT_EQ(txq_->write_ptr, 0);
    int available_space = iwl_queue_space(trans_, txq_);
    ASSERT_EQ(available_space, TFD_QUEUE_SIZE_MAX - 1);
  }

  // Copy an arbitray packet / device command into the member variables:
  //
  //  * mac_pkt_
  //  * pkt_
  //  * dev_cmd_
  //
  void SetupTxPacket() {
    uint8_t mac_pkt[] = {
        0x08, 0x01,                          // frame_ctrl
        0x00, 0x00,                          // duration
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,  // MAC1
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC2
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // MAC3
        0x00, 0x00,                          // seq_ctrl
    };
    ASSERT_GE(sizeof(mac_pkt_), sizeof(mac_pkt));
    memcpy(mac_pkt_, mac_pkt, sizeof(mac_pkt));

    wlan_tx_packet_t pkt = {
        .packet_head =
            {
                .data_buffer = mac_pkt_,
                .data_size = sizeof(mac_pkt_),
            },
        .info =
            {
                .tx_flags = 0,
                .cbw = WLAN_CHANNEL_BANDWIDTH__20,
            },
    };
    pkt_ = pkt;

    iwl_device_cmd dev_cmd = {
        .hdr =
            {
                .cmd = TX_CMD,
            },
    };
    dev_cmd_ = dev_cmd;
  }

 protected:
  int txq_id_;
  struct iwl_txq* txq_;
  uint8_t mac_pkt_[2048];
  wlan_tx_packet_t pkt_;
  iwl_device_cmd dev_cmd_;
};

TEST_F(TxTest, Init) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  for (int txq_id = 0; txq_id < base_params_.num_of_queues; txq_id++) {
    struct iwl_txq* queue = trans_pcie_->txq[txq_id];
    ASSERT_NOT_NULL(queue);
    EXPECT_EQ(queue->write_ptr, 0);
    EXPECT_EQ(queue->read_ptr, 0);
  }
}

TEST_F(TxTest, AsyncHostCommandEmpty) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  struct iwl_host_cmd hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = 0;
  hcmd.data[0] = NULL;

  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));
}

TEST_F(TxTest, AsyncHostCommandOneFragment) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  uint8_t fragment[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment);
  hcmd.data[0] = fragment;

  struct iwl_txq* txq = trans_pcie_->txq[trans_pcie_->cmd_queue];
  int cmd_idx = iwl_pcie_get_cmd_index(txq, txq->write_ptr);

  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));

  struct iwl_device_cmd* out_cmd =
      static_cast<iwl_device_cmd*>(io_buffer_virt(&txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment, sizeof(fragment));
}

TEST_F(TxTest, AsyncHostCommandTwoFragments) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  uint8_t fragment1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  uint8_t fragment2[] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment1);
  hcmd.data[0] = fragment1;
  hcmd.len[1] = sizeof(fragment2);
  hcmd.data[1] = fragment2;

  struct iwl_txq* txq = trans_pcie_->txq[trans_pcie_->cmd_queue];
  int cmd_idx = iwl_pcie_get_cmd_index(txq, txq->write_ptr);

  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));

  struct iwl_device_cmd* out_cmd =
      static_cast<iwl_device_cmd*>(io_buffer_virt(&txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment1, sizeof(fragment1));
  EXPECT_BYTES_EQ(out_cmd->payload + sizeof(fragment1), fragment2, sizeof(fragment2));
}

TEST_F(TxTest, AsyncLargeHostCommands) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  std::array<uint8_t, TFD_MAX_PAYLOAD_SIZE> fragment = {0};
  struct iwl_host_cmd default_hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  default_hcmd.len[0] = sizeof(fragment);
  default_hcmd.data[0] = fragment.data();
  EXPECT_EQ(iwl_trans_pcie_send_hcmd(trans_, &default_hcmd), ZX_ERR_INVALID_ARGS);

  struct iwl_host_cmd nocopy_hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  nocopy_hcmd.len[0] = sizeof(fragment);
  nocopy_hcmd.data[0] = fragment.data();
  nocopy_hcmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
  EXPECT_EQ(iwl_trans_pcie_send_hcmd(trans_, &nocopy_hcmd), ZX_OK);

  struct iwl_host_cmd dup_hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  dup_hcmd.len[0] = sizeof(fragment);
  dup_hcmd.data[0] = fragment.data();
  dup_hcmd.dataflags[0] = IWL_HCMD_DFL_DUP;

  EXPECT_EQ(iwl_trans_pcie_send_hcmd(trans_, &dup_hcmd), ZX_OK);
}

TEST_F(TxTest, SyncTwoFragmentsWithOneDup) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  // Must be long enough so that len(fragment1+iwl_cmd_header) >= IWL_FIRST_TB_SIZE(20)
  uint8_t fragment0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t fragment1[TFD_MAX_PAYLOAD_SIZE] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment0);
  hcmd.data[0] = fragment0;
  hcmd.len[1] = sizeof(fragment1);
  hcmd.data[1] = fragment1;
  hcmd.dataflags[1] = IWL_HCMD_DFL_DUP;

  // Save the orginal command index
  struct iwl_txq* txq = trans_pcie_->txq[trans_pcie_->cmd_queue];
  int cmd_idx = iwl_pcie_get_cmd_index(txq, txq->write_ptr);

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));

  struct iwl_device_cmd* out_cmd =
      static_cast<iwl_device_cmd*>(io_buffer_virt(&txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment0, sizeof(fragment0));
  EXPECT_BYTES_EQ(out_cmd->payload + sizeof(fragment0), fragment1, sizeof(fragment1));
  ASSERT_TRUE(io_buffer_is_valid(&txq->entries[cmd_idx].dup_io_buf));
  uint8_t* dup_buf = static_cast<uint8_t*>(io_buffer_virt(&txq->entries[cmd_idx].dup_io_buf));
  EXPECT_BYTES_EQ(dup_buf, fragment1, sizeof(fragment1));
}

TEST_F(TxTest, SyncTwoFragmentsWithTwoDups) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  // Must be long enough so that len(fragment1+iwl_cmd_header) >= IWL_FIRST_TB_SIZE(20)
  uint8_t fragment0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t fragment1[] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment0);
  hcmd.data[0] = fragment0;
  hcmd.dataflags[0] = IWL_HCMD_DFL_DUP;
  hcmd.len[1] = sizeof(fragment1);
  hcmd.data[1] = fragment1;
  hcmd.dataflags[1] = IWL_HCMD_DFL_DUP;

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_EQ(iwl_trans_pcie_send_hcmd(trans_, &hcmd), ZX_ERR_INVALID_ARGS);
}

TEST_F(TxTest, SyncTwoFragmentsWithOneNocopy) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  // Must be long enough so that len(fragment1+iwl_cmd_header) >= IWL_FIRST_TB_SIZE(20)
  uint8_t fragment0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t fragment1[TFD_MAX_PAYLOAD_SIZE] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment0);
  hcmd.data[0] = fragment0;
  hcmd.len[1] = sizeof(fragment1);
  hcmd.data[1] = fragment1;
  hcmd.dataflags[1] = IWL_HCMD_DFL_NOCOPY;

  // Save the orginal command index
  struct iwl_txq* txq = trans_pcie_->txq[trans_pcie_->cmd_queue];
  int cmd_idx = iwl_pcie_get_cmd_index(txq, txq->write_ptr);

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));

  struct iwl_device_cmd* out_cmd =
      static_cast<iwl_device_cmd*>(io_buffer_virt(&txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment0, sizeof(fragment0));
  EXPECT_BYTES_EQ(out_cmd->payload + sizeof(fragment0), fragment1, sizeof(fragment1));
  ASSERT_TRUE(io_buffer_is_valid(&txq->entries[cmd_idx].dup_io_buf));
  uint8_t* dup_buf = static_cast<uint8_t*>(io_buffer_virt(&txq->entries[cmd_idx].dup_io_buf));
  EXPECT_BYTES_EQ(dup_buf, fragment1, sizeof(fragment1));
}

TEST_F(TxTest, SyncTwoFragmentsWithFirstDup) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  // Must be long enough so that len(fragment1+iwl_cmd_header) >= IWL_FIRST_TB_SIZE(20)
  uint8_t fragment0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t fragment1[] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment0);
  hcmd.data[0] = fragment0;
  hcmd.dataflags[0] = IWL_HCMD_DFL_DUP;
  hcmd.len[1] = sizeof(fragment1);
  hcmd.data[1] = fragment1;

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_trans_pcie_send_hcmd(trans_, &hcmd));
}

TEST_F(TxTest, SyncTwoFragmentsWithFirstNocopy) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  // Must be long enough so that len(fragment1+iwl_cmd_header) >= IWL_FIRST_TB_SIZE(20)
  uint8_t fragment0[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t fragment1[TFD_MAX_PAYLOAD_SIZE] = {0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = sizeof(fragment0);
  hcmd.data[0] = fragment0;
  hcmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
  hcmd.len[1] = sizeof(fragment1);
  hcmd.data[1] = fragment1;

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_trans_pcie_send_hcmd(trans_, &hcmd));
}

// This test is going to go through the sync host command call:
//
// + iwl_trans_pcie_send_hcmd() will call iwl_pcie_send_hcmd_sync().
// + iwl_pcie_send_hcmd_sync() then calls iwl_pcie_enqueue_hcmd(), which triggers
//   GenerateFakeEchoResponse().
// + A fake response is generated to call iwl_pcie_hcmd_complete().
// + trans_pcie->wait_command_queue is then signaled in iwl_pcie_hcmd_complete().
// + iwl_pcie_send_hcmd_sync() checks the values after trans_pcie->wait_command_queue.
// + ZX_OK if everything looks good.
//
TEST_F(TxTest, SyncHostCommandEmpty) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));
  struct iwl_host_cmd hcmd = {
      .flags = CMD_WANT_SKB,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = 0;
  hcmd.data[0] = NULL;

  trans_ops_.write32 = FakeEchoWrite32;
  ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));
}

//
// This test first enqueues half number of txq->n_window commands. Call unmap. Enqueues
// half of txq->n_window + 1 to ensure the wrap case has been covered.
//
// Since unref doesn't do anything for now, we don't test the non-command-queue case.
//
TEST_F(TxTest, UnmapCmdQueue) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  // We avoid using FakeEchoWrite32 so that commands will stay in flight and
  // not get completed. This allows us to test the unmap API
  //
  // Note, we need the commands to be ASYNC to avoid timeout error.
  struct iwl_host_cmd hcmd = {
      .flags = CMD_ASYNC,
      .id = ECHO_CMD,
  };
  hcmd.len[0] = 0;
  hcmd.data[0] = NULL;

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
  int txq_id = trans_pcie->cmd_queue;
  struct iwl_txq* txq = trans_pcie->txq[txq_id];

  // Adds first half of commands.
  int half = (txq->n_window + 1) / 2;
  for (int i = 0; i < half; ++i) {
    ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));
    EXPECT_TRUE(trans_pcie->ref_cmd_in_flight);
  }
  iwl_pcie_txq_unmap(trans_, trans_pcie->cmd_queue);
  EXPECT_FALSE(trans_pcie->ref_cmd_in_flight);

  // Adds another half and plus one to ensure wrapping.
  for (int i = 0; i < half + 1; ++i) {
    ASSERT_OK(iwl_trans_pcie_send_hcmd(trans_, &hcmd));
    EXPECT_TRUE(trans_pcie->ref_cmd_in_flight);
  }
  iwl_pcie_txq_unmap(trans_, trans_pcie->cmd_queue);
  EXPECT_FALSE(trans_pcie->ref_cmd_in_flight);

  // All num_tbs should have been cleared to zero.
  for (int index = 0; index < txq->n_window; ++index) {
    void* tfd = iwl_pcie_get_tfd(trans_, txq, index);
    if (trans_->cfg->use_tfh) {
      struct iwl_tfh_tfd* tfd_fh = static_cast<struct iwl_tfh_tfd*>(tfd);
      EXPECT_EQ(0, tfd_fh->num_tbs);
    } else {
      struct iwl_tfd* tfd_fh = static_cast<struct iwl_tfd*>(tfd);
      EXPECT_EQ(0, tfd_fh->num_tbs);
    }
  }
}

static zx_status_t iwl_pcie_cmdq_reclaim_locked(struct iwl_trans* trans, int txq_id, uint32_t idx) {
  if (txq_id < 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct iwl_txq* txq = IWL_TRANS_GET_PCIE_TRANS(trans)->txq[txq_id];

  mtx_lock(&txq->lock);
  zx_status_t ret = iwl_pcie_cmdq_reclaim(trans, txq_id, idx);
  mtx_unlock(&txq->lock);

  return ret;
}

//
// Ensure iwl_pcie_cmdq_reclaim() operates only on cmd_queue.
//
TEST_F(TxTest, ReclaimCmdQueueInvalidQueueID) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
  int txq_id = trans_pcie->cmd_queue;
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  txq->wd_timeout = 0;
  txq->read_ptr = 0;
  txq->write_ptr = 1;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id + 1, 0));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id - 1, 0));
  ASSERT_EQ(ZX_OK, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 0));
}

//
// Test for handling of invalid index values.
//
TEST_F(TxTest, ReclaimCmdQueueInvalidIndex) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
  int txq_id = trans_pcie->cmd_queue;
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  txq->wd_timeout = 0;
  txq->read_ptr = 0;
  txq->write_ptr = 1;

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 1));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 2));

  uint32_t idx = trans_->cfg->base_params->max_tfd_queue_size + 1;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, idx));

  ASSERT_EQ(ZX_OK, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 0));
}

//
// The iwl_pcie_cmdq_reclaim() expects the passed index to reclaim exactly
// a single slot. It considers any index passed that requires multiple reclaim
// as an error.
//
TEST_F(TxTest, ReclaimCmdQueueMultiReclaim) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
  int txq_id = trans_pcie->cmd_queue;
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  txq->wd_timeout = 0;
  txq->read_ptr = 0;
  txq->write_ptr = 3;

  // In this case, the read_ptr will have to be advanced more than once
  // to match the passed index of 2. This should be considered as bad state.
  ASSERT_EQ(ZX_ERR_BAD_STATE, iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 2));
}

//
// Ensure the inflight flag is cleared (or not) as appropriate.
//
TEST_F(TxTest, ReclaimCmdQueueInFlightFlag) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans_);
  int txq_id = trans_pcie->cmd_queue;
  struct iwl_txq* txq = trans_pcie->txq[txq_id];
  txq->wd_timeout = 0;
  txq->read_ptr = 2;
  txq->write_ptr = 4;
  trans_pcie->ref_cmd_in_flight = true;

  iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 2);
  ASSERT_TRUE(trans_pcie->ref_cmd_in_flight);

  // Once index 3 is reclaimed, the readptr will move to 4 which should
  // make it equal to the write_ptr. This will clear the inflight flag.
  iwl_pcie_cmdq_reclaim_locked(trans_, txq_id, 3);
  ASSERT_FALSE(trans_pcie->ref_cmd_in_flight);
}

TEST_F(TxTest, TxDataCornerCaseUnusedQueue) {
  ASSERT_OK(iwl_pcie_tx_init(trans_));

  wlan_tx_packet_t pkt = {};
  iwl_device_cmd dev_cmd = {};
  // unused queue
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            iwl_trans_pcie_tx(trans_, &pkt, &dev_cmd, /* txq_id */ IWL_MVM_DQA_MIN_DATA_QUEUE));
}

TEST_F(TxTest, TxDataCornerPacketTail) {
  SetupTxQueue();
  SetupTxPacket();

  wlan_tx_packet_t pkt = pkt_;
  pkt.packet_tail_count = 1;  // whatever non-zero value.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, iwl_trans_pcie_tx(trans_, &pkt, &dev_cmd_, txq_id_));
}

TEST_F(TxTest, TxNormal) {
  SetupTxQueue();
  SetupTxPacket();

  ref_.ExpectCall();
  ASSERT_EQ(0, txq_->read_ptr);
  ASSERT_EQ(0, txq_->write_ptr);
  // Tx a packet and see the write pointer advanced.
  ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, &pkt_, &dev_cmd_, txq_id_));
  ASSERT_EQ(0, txq_->read_ptr);
  ASSERT_EQ(1, txq_->write_ptr);
  ASSERT_EQ(TFD_QUEUE_SIZE_MAX - 1 - /* this packet */ 1, iwl_queue_space(trans_, txq_));
  ref_.VerifyAndClear();
}

TEST_F(TxTest, TxNormalThenReclaim) {
  SetupTxQueue();
  SetupTxPacket();

  ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, &pkt_, &dev_cmd_, txq_id_));

  unref_.ExpectCall();
  // reclaim a packet and see the reader pointer advanced.
  iwl_trans_pcie_reclaim(trans_, txq_id_, /*ssn*/ 1);
  ASSERT_EQ(1, txq_->write_ptr);
  unref_.VerifyAndClear();
}

// Note that even the number of queued packets exceed the high mark, the function still returns
// OK. Beside checking the txq->write_ptr, we also expect queue_full is called.
//
TEST_F(TxTest, TxSoManyPackets) {
  SetupTxQueue();
  SetupTxPacket();

  // Fill up all space.
  op_mode_queue_full_.ExpectCall(txq_id_);
  for (int i = 0; i < TFD_QUEUE_SIZE_MAX * 2; i++) {
    ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, &pkt_, &dev_cmd_, txq_id_));
    ASSERT_EQ(MIN(TFD_QUEUE_SIZE_MAX - TX_RESERVED_SPACE, i + 1), txq_->write_ptr);
  }
  op_mode_queue_full_.VerifyAndClear();
}

// Follow-up test of TxSoManyPackets(), but focus on queue_not_full.
//
TEST_F(TxTest, TxSoManyPacketsThenReclaim) {
  SetupTxQueue();
  SetupTxPacket();

  // Fill up all space.
  for (int i = 0; i < TFD_QUEUE_SIZE_MAX * 2; i++) {
    ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, &pkt_, &dev_cmd_, txq_id_));
  }

  op_mode_queue_not_full_.ExpectCall(txq_id_);
  // reclaim
  iwl_trans_pcie_reclaim(trans_, txq_id_, /*ssn*/ TFD_QUEUE_SIZE_MAX - TX_RESERVED_SPACE);
  // We don't have much to check. But at least we can ensure the call doesn't crash.
  op_mode_queue_not_full_.VerifyAndClear();
}

class StuckTimerTest : public PcieTest {
 public:
  StuckTimerTest() {
    ASSERT_OK(async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &trans_->loop));
    ASSERT_OK(async_loop_start_thread(trans_->loop, "iwlwifi-test-worker", NULL));

    mtx_init(&txq_.lock, mtx_plain);
    iwlwifi_timer_init(trans_, &txq_.stuck_timer);

    // Set read and write pointers such that firing the stuck timer is valid.
    txq_.write_ptr = 0;
    txq_.read_ptr = 1;
  }

  ~StuckTimerTest() { free(trans_->loop); }

  void WaitForWorkerThread() {
    async_loop_quit(trans_->loop);
    async_loop_join_threads(trans_->loop);
  }

 protected:
  struct iwl_txq txq_;
};

TEST_F(StuckTimerTest, SetTimer) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, SetSpuriousTimer) {
  // Set read and write pointers such that firing the stuck timer is spurious.
  txq_.write_ptr = 0;
  txq_.read_ptr = 0;
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, SetTimerOverride) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE);
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);
  sync_completion_wait(&txq_.stuck_timer.finished, ZX_TIME_INFINITE);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, StopPendingTimer) {
  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE);
  // Test that stop doesn't deadlock.
  iwlwifi_timer_stop(&txq_.stuck_timer);
  WaitForWorkerThread();
}

TEST_F(StuckTimerTest, StopUnsetTimer) {
  // Test that stop doesn't deadlock.
  iwlwifi_timer_stop(&txq_.stuck_timer);
  WaitForWorkerThread();
}

// This test is a best-effort attempt to test that a race condition is correctly handled. The test
// should pass both when the race condition is and isn't triggered.
TEST_F(StuckTimerTest, StopRunningTimer) {
  // Hold the txq lock so that the timer handler blocks.
  mtx_lock(&txq_.lock);

  iwlwifi_timer_set(&txq_.stuck_timer, ZX_TIME_INFINITE_PAST);

  // Sleep to give the timer thread a chance to schedule.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  // Call stop on a new thread, it will block until the handler completes.
  std::thread stop_thread(iwlwifi_timer_stop, &txq_.stuck_timer);

  // Sleep to give the stop thread a chance to schedule.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));

  // Unblock the timer thread.
  mtx_unlock(&txq_.lock);

  // Wait for all the threads to finish. This tests that the timer doesn't deadlock. We can't check
  // the value of finished, since there's a chance that the race condition we're trying to trigger
  // didn't happen (i.e. the handler didn't fire before we called stop, or stop didn't block before
  // the handler was unblocked.)
  WaitForWorkerThread();
  stop_thread.join();
}

}  // namespace
