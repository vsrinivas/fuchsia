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

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake-bti/bti.h>
#include <lib/mock-function/mock-function.h>
#include <lib/sync/completion.h>
#include <lib/zx/bti.h>
#include <zircon/listnode.h>

#include <array>
#include <string>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-fh.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-csr.h"
}
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/pcie/internal.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/align.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/irq.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/memory.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/pcie-device.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/stats.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/wlan-pkt-builder.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace {

constexpr int kTestDeviceId = 0x095a;
constexpr int kTestSubsysDeviceId = 0x9e10;

TEST(MockDdkTesterPci, DeviceLifeCycle) {
  auto parent = MockDevice::FakeRootParent();

  // Set up a fake pci:
  pci::FakePciProtocol fake_pci;
  // Set up the first BAR.
  fake_pci.CreateBar(/*bar_id=*/0, /*size=*/4096, /*is_mmio=*/true);

  // Identify as the correct device.
  fake_pci.SetDeviceInfo({.device_id = kTestDeviceId});
  zx::unowned_vmo config = fake_pci.GetConfigVmo();
  config->write(&kTestSubsysDeviceId, PCI_CONFIG_SUBSYSTEM_ID, sizeof(kTestSubsysDeviceId));

  // Need an IRQ of some kind. Since Intel drivers are very specific in their
  // MSI-X handling we'll keep it simple and use a legacy interrupt.
  fake_pci.AddLegacyInterrupt();

  // Now add the protocol to the parent.
  // PCI is the only protocol of interest here.
  async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
  parent->AddFidlProtocol(
      fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
      [&loop, &fake_pci](zx::channel channel) {
        fidl::BindServer(loop.dispatcher(),
                         fidl::ServerEnd<fuchsia_hardware_pci::Device>(std::move(channel)),
                         &fake_pci);
        return ZX_OK;
      },
      "pci");
  loop.StartThread("pci-fidl-server-thread");

  // Create() allocates and binds the device.
  ASSERT_OK(wlan::iwlwifi::PcieDevice::Create(parent.get()), "Bind failed");

  auto& pcie_device = parent->children().front();
  // Set up a fake firmware of non-zero size for the PcieDevice:
  // TODO(fxbug.dev/76744) since device initialization will fail (as there is no hardware backing
  // this PcieDevice), we are free to use a fake firmware here that does not pass driver validation
  // anyways.
  pcie_device->SetFirmware(std::string(4, '\0'));

  // TODO(fxbug.dev/76744) the Create() call will succeed, but since there is no hardware backing
  // this PcieDevice, initialization will ultimately fail and the PcieDevice instance will be
  // automatically removed without explicitly reporting an error.
  pcie_device->InitOp();  // Calls DdkInit for ddktl devices.

  // If another thread is spawned during the init call, wait until InitReply is called:
  pcie_device->WaitUntilInitReplyCalled();
  EXPECT_EQ(1, parent->child_count());

  device_async_remove(pcie_device.get());

  mock_ddk::ReleaseFlaggedDevices(pcie_device.get());
}

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

  struct iwl_iobuf* io_buf = nullptr;
  ASSERT_OK(iwl_iobuf_allocate_contiguous(trans->dev, 128, &io_buf));
  struct iwl_rx_cmd_buffer rxcb = {
      ._iobuf = io_buf,
      ._offset = 0,
  };
  struct iwl_rx_packet* resp_pkt =
      reinterpret_cast<struct iwl_rx_packet*>(iwl_iobuf_virtual(io_buf));
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

  iwl_iobuf_release(io_buf);
  io_buf = nullptr;
}
#pragma GCC diagnostic pop

class PcieTest : public zxtest::Test {
 public:
  PcieTest() {
    task_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(task_loop_->StartThread("iwlwifi-test-task-worker", nullptr));
    irq_loop_ = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_OK(irq_loop_->StartThread("iwlwifi-test-irq-worker", nullptr));
    pci_dev_.dev.task_dispatcher = task_loop_->dispatcher();
    pci_dev_.dev.irq_dispatcher = irq_loop_->dispatcher();
    fake_bti_create(&pci_dev_.dev.bti);

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
    trans_ =
        iwl_trans_alloc(sizeof(struct iwl_trans_pcie_wrapper), &pci_dev_.dev, &cfg_, &trans_ops_);
    ASSERT_NE(trans_, nullptr);
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans_));
    wrapper->test = this;
    trans_pcie_ = &wrapper->trans_pcie;
    trans_pcie_->pci_dev = &pci_dev_;

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
    iwl_trans_free(trans_);
    zx_handle_close(pci_dev_.dev.bti);
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

  mock_function::MockFunction<zx_status_t, uint32_t, void*, size_t> mock_read_mem_;
  static zx_status_t read_mem_wrapper(struct iwl_trans* trans, uint32_t addr, void* buf,
                                      size_t dwords) {
    auto wrapper = reinterpret_cast<iwl_trans_pcie_wrapper*>(IWL_TRANS_GET_PCIE_TRANS(trans));
    if (wrapper->test->mock_read_mem_.HasExpectations()) {
      return wrapper->test->mock_read_mem_.Call(addr, buf, dwords);
    } else {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  mock_function::MockFunction<zx_status_t, uint32_t, const void*, size_t> mock_write_mem_;
  static zx_status_t write_mem_wrapper(struct iwl_trans* trans, uint32_t addr, const void* buf,
                                       size_t dwords) {
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
      for (size_t offset = 0; offset < iwl_iobuf_size(rxb->io_buf);
           offset += IWL_ALIGN(1, FH_RSCSR_FRAME_ALIGN)) {  // move to next packet
        struct iwl_rx_cmd_buffer rxcb = {
            ._iobuf = rxb->io_buf,
            ._offset = static_cast<int>(offset),
        };
        struct iwl_rx_packet* pkt = (struct iwl_rx_packet*)rxb_addr(&rxcb);
        pkt->hdr.sequence |= SEQ_RX_FRAME;
      }
    }
  }

 protected:
  std::unique_ptr<::async::Loop> task_loop_;
  std::unique_ptr<::async::Loop> irq_loop_;
  struct iwl_pci_dev pci_dev_ = {};
  struct iwl_trans* trans_ = {};
  struct iwl_trans_pcie* trans_pcie_ = {};
  struct iwl_base_params base_params_ = {};
  struct iwl_cfg cfg_ = {};
  struct iwl_trans_ops trans_ops_ = {};
  struct iwl_op_mode op_mode_ = {};
  struct iwl_op_mode_ops op_mode_ops_ = {};
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
  EXPECT_NOT_NULL(trans_pcie_->rxq->rb_status);

  struct iwl_rx_mem_buffer* rxb;
  list_for_every_entry (&trans_pcie_->rxq->rx_free, rxb, struct iwl_rx_mem_buffer, list) {
    EXPECT_NOT_NULL(rxb->io_buf);
    // rx_buf_size is IWL_AMSDU_2K, but the minimum allocatd buffer size is 1 page.
    EXPECT_EQ(iwl_iobuf_size(rxb->io_buf), 4 * 1024);
  }

  iwl_pcie_rx_free(trans_);
}

// Test the multipe Rx queues initialization.
TEST_F(PcieTest, MqRxInit) {
  cfg_.mq_rx_supported = true;
  trans_->num_rx_queues = 16;
  trans_pcie_->rx_buf_size = IWL_AMSDU_2K;

  // This is the value we get when we call io_buffer_phys() in the fake environment.
  const size_t kFakePhys = FAKE_BTI_PHYS_ADDR;

  // Set expectation for register accesses.
  mock_write_prph_.ExpectCall(RFH_RXF_DMA_CFG, 0);
  mock_write_prph_.ExpectCall(RFH_RXF_RXQ_ACTIVE, 0);
  for (int i = 0; i < trans_->num_rx_queues; i++) {
    mock_write_prph_.ExpectCall(RFH_Q_FRBDCB_BA_LSB(i), kFakePhys);
    mock_write_prph_.ExpectCall(RFH_Q_FRBDCB_BA_LSB(i) + 4, 0);  // MSB of a 64-bit
    mock_write_prph_.ExpectCall(RFH_Q_URBDCB_BA_LSB(i), kFakePhys);
    mock_write_prph_.ExpectCall(RFH_Q_URBDCB_BA_LSB(i) + 4, 0);  // MSB of a 64-bit
    mock_write_prph_.ExpectCall(RFH_Q_URBD_STTS_WPTR_LSB(i), kFakePhys);
    mock_write_prph_.ExpectCall(RFH_Q_URBD_STTS_WPTR_LSB(i) + 4, 0);  // MSB of a 64-bit
    mock_write_prph_.ExpectCall(RFH_Q_FRBDCB_WIDX(i), 0);
    mock_write_prph_.ExpectCall(RFH_Q_FRBDCB_RIDX(i), 0);
    mock_write_prph_.ExpectCall(RFH_Q_URBDCB_WIDX(i), 0);
  }

  mock_write_prph_.ExpectCall(
      RFH_RXF_DMA_CFG, RFH_DMA_EN_ENABLE_VAL | RFH_RXF_DMA_RB_SIZE_2K | RFH_RXF_DMA_MIN_RB_4_8 |
                           RFH_RXF_DMA_DROP_TOO_LARGE_MASK | RFH_RXF_DMA_RBDCB_SIZE_512);

  mock_write_prph_.ExpectCall(RFH_GEN_CFG,
                              RFH_GEN_CFG_RFH_DMA_SNOOP | RFH_GEN_CFG_VAL(DEFAULT_RXQ_NUM, 0) |
                                  RFH_GEN_CFG_SERVICE_DMA_SNOOP |
                                  RFH_GEN_CFG_VAL(RB_CHUNK_SIZE, RFH_GEN_CFG_RB_CHUNK_SIZE_128));

  // BIT(i) and BIT(i+16) for each i in trans_->num_rx_queues
  mock_write_prph_.ExpectCall(RFH_RXF_RXQ_ACTIVE, 0xffffffff);

  // Run it!
  iwl_pcie_rx_init(trans_);

  // Check results.
  EXPECT_NOT_NULL(trans_pcie_->rxq->rb_status);
  mock_write_prph_.VerifyAndClear();

  iwl_pcie_rx_free(trans_);
}

TEST_F(PcieTest, IctTable) {
  ASSERT_OK(iwl_pcie_alloc_ict(trans_));

  // Check the initial state.
  ASSERT_NOT_NULL(trans_pcie_->ict_tbl);
  EXPECT_EQ(iwl_pcie_int_cause_ict(trans_), 0);
  EXPECT_EQ(trans_pcie_->ict_index, 0);

  // Reads an interrupt from the table.
  uint32_t* ict_table = static_cast<uint32_t*>(iwl_iobuf_virtual(trans_pcie_->ict_tbl));
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
  int ict_count = static_cast<int>(iwl_iobuf_size(trans_pcie_->ict_tbl) / sizeof(uint32_t));

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

  ASSERT_NOT_NULL(trans_pcie_->ict_tbl);
  uint32_t* ict_table = static_cast<uint32_t*>(iwl_iobuf_virtual(trans_pcie_->ict_tbl));

  // Spurious interrupt.
  trans_pcie_->ict_index = 0;
  ict_table[0] = 0;
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_INTS_FROM_FW), 0);
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_INTS_FROM_FW), 1);

  // Set up the ICT table with an RX interrupt at index 0.
  trans_pcie_->ict_index = 0;
  ict_table[0] = static_cast<uint32_t>(CSR_INT_BIT_FH_RX) >> 16;
  ict_table[1] = 0;

  // This struct is controlled by the device, closed_rb_num is the read index for the shared ring
  // buffer. For the tests we manually increment the index to push forward the index.
  struct iwl_rb_status* rb_status =
      static_cast<struct iwl_rb_status*>(iwl_iobuf_virtual(trans_pcie_->rxq->rb_status));

  // Process 128 buffers. The driver should process each buffer and indicate this to the hardware by
  // setting the write index (rxq->write_actual).
  rb_status->closed_rb_num = 128;
  markUcodeOrignated(0, 127);
  ASSERT_OK(iwl_pcie_isr(trans_));
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_INTS_FROM_FW), 2);
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
  EXPECT_EQ(iwl_stats_read(IWL_STATS_CNT_INTS_FROM_FW), 3);
  EXPECT_EQ(trans_pcie_->rxq->write, 171);
  EXPECT_EQ(trans_pcie_->rxq->write_actual, 168);

  iwl_pcie_rx_free(trans_);
  iwl_pcie_tx_free(trans_);
  iwl_pcie_free_ict(trans_);
}

class TxTest : public PcieTest {
  void SetUp() {
    base_params_.num_of_queues = 31;
    base_params_.max_tfd_queue_size = 256;
    trans_pcie_->tfd_size = sizeof(struct iwl_tfh_tfd);
    trans_pcie_->max_tbs = IWL_NUM_OF_TBS;
  }

  void TearDown() { iwl_pcie_tx_free(trans_); }

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

  // Initialize the memory variables for testing.
  //
  //  * wlan_pkt_: to hold the WLAN packet content.
  //  * dev_cmd_
  //
  void SetupTxPacket() {
    wlan::testing::WlanPktBuilder builder;
    wlan_pkt_ = builder.build();

    iwl_device_cmd dev_cmd = {
        .hdr =
            {
                .cmd = TX_CMD,
            },
    };
    dev_cmd_ = dev_cmd;
  }

  struct iwl_tfd* GetLastTfd() const {
    return static_cast<struct iwl_tfd*>(iwl_pcie_get_tfd(trans_, txq_, txq_->write_ptr));
  }

  void CheckTb(zx_paddr_t expected_addr, uint16_t expected_len, uint32_t tb_idx) const {
    auto* tfd = GetLastTfd();

    ASSERT_TRUE(tfd->num_tbs > tb_idx);

    auto& tb = tfd->tbs[tb_idx];

    ASSERT_EQ(cpu_to_le32(expected_addr), tb.lo);

    auto hi = tb.hi_n_len & TB_HI_N_LEN_ADDR_HI_MSK;
    auto len = (tb.hi_n_len & TB_HI_N_LEN_LEN_MSK) >> 4;

    ASSERT_EQ(iwl_get_dma_hi_addr(expected_addr), hi);
    ASSERT_EQ(expected_len, len);
  }

 protected:
  int txq_id_;
  struct iwl_txq* txq_;
  std::shared_ptr<wlan::testing::WlanPktBuilder::WlanPkt> wlan_pkt_;
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
      static_cast<iwl_device_cmd*>(iwl_iobuf_virtual(txq->entries[cmd_idx].cmd));
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
      static_cast<iwl_device_cmd*>(iwl_iobuf_virtual(txq->entries[cmd_idx].cmd));
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
      static_cast<iwl_device_cmd*>(iwl_iobuf_virtual(txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment0, sizeof(fragment0));
  EXPECT_BYTES_EQ(out_cmd->payload + sizeof(fragment0), fragment1, sizeof(fragment1));
  ASSERT_NOT_NULL(txq->entries[cmd_idx].dup_io_buf);
  uint8_t* dup_buf = static_cast<uint8_t*>(iwl_iobuf_virtual(txq->entries[cmd_idx].dup_io_buf));
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
      static_cast<iwl_device_cmd*>(iwl_iobuf_virtual(txq->entries[cmd_idx].cmd));
  EXPECT_BYTES_EQ(out_cmd->payload, fragment0, sizeof(fragment0));
  EXPECT_BYTES_EQ(out_cmd->payload + sizeof(fragment0), fragment1, sizeof(fragment1));
  ASSERT_NOT_NULL(txq->entries[cmd_idx].dup_io_buf);
  uint8_t* dup_buf = static_cast<uint8_t*>(iwl_iobuf_virtual(txq->entries[cmd_idx].dup_io_buf));
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

  ieee80211_mac_packet pkt = {};
  iwl_device_cmd dev_cmd = {};
  // unused queue
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            iwl_trans_pcie_tx(trans_, &pkt, &dev_cmd, /* txq_id */ IWL_MVM_DQA_MIN_DATA_QUEUE));
}

TEST_F(TxTest, TxNormal) {
  SetupTxQueue();
  SetupTxPacket();

  ref_.ExpectCall();
  ASSERT_EQ(0, txq_->read_ptr);
  ASSERT_EQ(0, txq_->write_ptr);
  // Tx a packet and see the write pointer advanced.
  ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, wlan_pkt_->mac_pkt(), &dev_cmd_, txq_id_));
  ASSERT_EQ(0, txq_->read_ptr);
  ASSERT_EQ(1, txq_->write_ptr);
  ASSERT_EQ(TFD_QUEUE_SIZE_MAX - 1 - /* this packet */ 1, iwl_queue_space(trans_, txq_));
  ref_.VerifyAndClear();

  // reclaim a packet and see the writer pointer advanced.
  unref_.ExpectCall();
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

  op_mode_queue_full_.ExpectCall(txq_id_);
  // Fill up all space.
  for (int i = 0; i < TFD_QUEUE_SIZE_MAX * 2; i++) {
    ASSERT_EQ(ZX_OK, iwl_trans_pcie_tx(trans_, wlan_pkt_->mac_pkt(), &dev_cmd_, txq_id_));
  }

  op_mode_queue_not_full_.ExpectCall(txq_id_);
  // reclaim
  iwl_trans_pcie_reclaim(trans_, txq_id_, /*ssn*/ TFD_QUEUE_SIZE_MAX - TX_RESERVED_SPACE);
  // We don't have much to check. But at least we can ensure the call doesn't crash.
  op_mode_queue_not_full_.VerifyAndClear();
  op_mode_queue_full_.VerifyAndClear();
}

//
// Test that iwl_pcie_txq_build_tfd succeeds for expected cases.
// This test adds 4 TBs to the last TFD and checks that the correct addresses/lengths are
// added to the TB.
// All calls to iwl_pcie_txq_build_tfd in this test are expected to succeed.
//
TEST_F(TxTest, BuildTfdSucceeds) {
  SetupTxQueue();

  std::vector<std::pair<zx_paddr_t, uint16_t>> tb_addr_and_len{
      {1234, 1},
      {456789, 1000},
      {0xDABCDABCD, 123},  // check that hi address gets set
      {123456, 0xfff},     // check max length
  };

  uint32_t num_tbs = 0;
  uint32_t tb_idx = 0;

  for (const auto& [addr, len] : tb_addr_and_len) {
    zx_status_t status = iwl_pcie_txq_build_tfd(trans_, txq_, addr, len, false, &num_tbs);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(num_tbs, tb_idx);
    CheckTb(addr, len, tb_idx);
    ++tb_idx;
  }

  ASSERT_EQ(tb_addr_and_len.size(), GetLastTfd()->num_tbs);
}

//
// Test that the `reset` parameter of iwl_pcie_txq_build_tfd works as expected.
// The reset parameter is expected to erase any previous TBs in the TFD, so the test checks that
// tfd->num_tbs is set back to 1 and the first TB in the TFD contains the address/len passed to
// iwl_pcie_txq_build_tfd when reset was true.
//
TEST_F(TxTest, BuildTfdReset) {
  SetupTxQueue();

  uint32_t num_tbs = 0;

  zx_status_t status = iwl_pcie_txq_build_tfd(trans_, txq_, 12345, 100, false, &num_tbs);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(0, num_tbs);
  CheckTb(12345, 100, 0);

  // build_tfd with reset and check that first tb is overwritten
  status = iwl_pcie_txq_build_tfd(trans_, txq_, 45678, 1234, true, &num_tbs);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(0, num_tbs);
  CheckTb(45678, 1234, 0);

  ASSERT_EQ(1, GetLastTfd()->num_tbs);
}

TEST_F(TxTest, BuildTfdDmaAddressTooLarge) {
  SetupTxQueue();
  uint32_t num_tbs = 0;
  zx_status_t status = iwl_pcie_txq_build_tfd(trans_, txq_, 0x1000000000, 100, false, &num_tbs);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST_F(TxTest, BuildTfdTooManyTbs) {
  SetupTxQueue();

  zx_status_t status;
  uint32_t num_tbs = 0;

  for (unsigned i = 0; i < trans_pcie_->max_tbs; i++) {
    status = iwl_pcie_txq_build_tfd(trans_, txq_, 1234, 100, false, &num_tbs);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(i, num_tbs);
    CheckTb(1234, 100, i);
  }

  status = iwl_pcie_txq_build_tfd(trans_, txq_, 5678, 456, false, &num_tbs);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

  // check that no tbs got overwritten after trying to add another tb when the tfd was full
  for (unsigned i = 0; i < trans_pcie_->max_tbs; i++) {
    CheckTb(1234, 100, i);
  }
}

}  // namespace
