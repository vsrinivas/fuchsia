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
  resp_pkt->hdr.sequence = 0;

  // iwl_pcie_hcmd_complete() will require the txq->lock. However, we already have done it in
  // iwl_trans_pcie_send_hcmd(). So release the lock before calling it. Note that this is safe
  // because in the test, it is always single thread and has no race.
  //
  // The GCC pragma is to depress the compile warning on mutex check.
  //
  struct iwl_trans_pcie* trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
  struct iwl_txq* txq = trans_pcie->txq[trans_pcie->cmd_queue];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wthread-safety-analysis"
  mtx_unlock(&txq->lock);
  iwl_pcie_hcmd_complete(trans, &rxcb);
  mtx_lock(&txq->lock);

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
    trans_ops_.grab_nic_access = grab_nic_access_wrapper;
    trans_ops_.release_nic_access = release_nic_access_wrapper;
    cfg_.base_params = &base_params_;
    trans_ = iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &cfg_, &trans_ops_);
    ASSERT_NE(trans_, nullptr);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
    fake_bti_create(&trans_pcie_->bti);
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

 protected:
  struct iwl_trans* trans_;
  struct iwl_trans_pcie* trans_pcie_;
  struct iwl_base_params base_params_;
  struct iwl_cfg cfg_;
  struct iwl_trans_ops trans_ops_;
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
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 127);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 120);

  // Reset the interrupt table.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // Process another 300 buffers, wrapping to the beginning of the circular buffer.
  rb_status->closed_rb_num = 172;
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(trans_pcie_->rxq->write, 171);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 168);

  iwl_pcie_rx_free(trans_);
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
  }
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
