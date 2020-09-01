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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/types.h>

#include <array>
#include <tuple>

#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/sdio.h>
#include <mock/ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/sdio.h>
#include <wifi/wifi-config.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

// These numbers come from real bugs.
#define NOT_ALIGNED_SIZE 1541
#define ALIGNED_SIZE 1544

// This is required to use ddk::MockSdio.
bool operator==(const sdio_rw_txn_t& lhs, const sdio_rw_txn_t& rhs) {
  return (lhs.addr == rhs.addr && lhs.data_size == rhs.data_size && lhs.incr == rhs.incr &&
          lhs.write == rhs.write && lhs.buf_offset == rhs.buf_offset);
}
bool operator==(const sdio_rw_txn_new_t& lhs, const sdio_rw_txn_new_t& rhs) { return false; }

zx_status_t get_wifi_metadata(struct brcmf_bus* bus, void* data, size_t exp_size, size_t* actual) {
  return device_get_metadata(bus->bus_priv.sdio->drvr->zxdev, DEVICE_METADATA_WIFI_CONFIG, data,
                             exp_size, actual);
}

namespace {

constexpr sdio_rw_txn MakeSdioTxn(uint32_t addr, uint32_t data_size, bool incr, bool write,
                                  bool use_dma = false, zx_handle_t dma_vmo = ZX_HANDLE_INVALID) {
  return {.addr = addr,
          .data_size = data_size,
          .incr = incr,
          .write = write,
          .use_dma = use_dma,
          .dma_vmo = dma_vmo,
          .virt_buffer = nullptr,
          .virt_size = 0,
          .buf_offset = 0};
}

class MockSdio : public ddk::MockSdio {
 public:
  zx_status_t SdioDoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                                        uint8_t* out_read_byte) override {
    auto ret = mock_do_vendor_control_rw_byte_.Call(write, addr, write_byte);
    if (out_read_byte != nullptr) {
      *out_read_byte = std::get<1>(ret);
    }

    return std::get<0>(ret);
  }
};

TEST(Sdio, IntrRegister) {
  fake_ddk::Bind ddk;

  wifi_config_t config = {
      .oob_irq_mode = ZX_INTERRUPT_MODE_LEVEL_LOW,
  };
  ddk.SetMetadata(&config, sizeof(config));

  brcmf_pub drvr = {};
  brcmf_sdio_dev sdio_dev = {};
  sdio_func func1 = {};
  MockSdio sdio1;
  MockSdio sdio2;
  ddk::MockGpio gpio;
  brcmf_bus bus_if = {};
  brcmf_mp_device settings = {};
  brcmf_sdio_pd sdio_settings = {};
  const struct brcmf_bus_ops sdio_bus_ops = {
      .get_wifi_metadata = get_wifi_metadata,
  };

  sdio_dev.func1 = &func1;
  sdio_dev.gpios[WIFI_OOB_IRQ_GPIO_INDEX] = *gpio.GetProto();
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();
  sdio_dev.drvr = &drvr;
  bus_if.bus_priv.sdio = &sdio_dev;
  bus_if.ops = &sdio_bus_ops;
  sdio_dev.bus_if = &bus_if;
  sdio_dev.settings = &settings;
  sdio_dev.settings->bus.sdio = &sdio_settings;

  gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
      .ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_LEVEL_LOW, zx::interrupt(ZX_HANDLE_INVALID));
  sdio1.ExpectEnableFnIntr(ZX_OK).ExpectDoVendorControlRwByte(
      ZX_OK, true, SDIO_CCCR_BRCM_SEPINT, SDIO_CCCR_BRCM_SEPINT_MASK | SDIO_CCCR_BRCM_SEPINT_OE, 0);
  sdio2.ExpectEnableFnIntr(ZX_OK);

  EXPECT_OK(brcmf_sdiod_intr_register(&sdio_dev));

  gpio.VerifyAndClear();
  sdio1.VerifyAndClear();
  sdio2.VerifyAndClear();
}

TEST(Sdio, IntrUnregister) {
  brcmf_pub drvr = {};
  brcmf_sdio_dev sdio_dev = {};
  sdio_func func1 = {};

  MockSdio sdio1;
  MockSdio sdio2;
  sdio_dev.func1 = &func1;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();
  sdio_dev.drvr = &drvr;
  sdio_dev.oob_irq_requested = true;

  sdio1.ExpectDoVendorControlRwByte(ZX_OK, true, 0xf2, 0, 0).ExpectDisableFnIntr(ZX_OK);
  sdio2.ExpectDisableFnIntr(ZX_OK);

  brcmf_sdiod_intr_unregister(&sdio_dev);

  sdio1.VerifyAndClear();
  sdio2.VerifyAndClear();

  sdio_dev = {};
  func1 = {};

  sdio_dev.func1 = &func1;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();
  sdio_dev.drvr = &drvr;
  sdio_dev.sd_irq_requested = true;

  sdio1.ExpectDisableFnIntr(ZX_OK);
  sdio2.ExpectDisableFnIntr(ZX_OK);

  brcmf_sdiod_intr_unregister(&sdio_dev);

  sdio1.VerifyAndClear();
  sdio2.VerifyAndClear();
}

TEST(Sdio, VendorControl) {
  brcmf_sdio_dev sdio_dev = {};

  MockSdio sdio1;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();

  sdio1.ExpectDoVendorControlRwByte(ZX_ERR_IO, false, 0xf0, 0, 0xab)
      .ExpectDoVendorControlRwByte(ZX_OK, false, 0xf3, 0, 0x12)
      .ExpectDoVendorControlRwByte(ZX_ERR_BAD_STATE, true, 0xff, 0x55, 0)
      .ExpectDoVendorControlRwByte(ZX_ERR_TIMED_OUT, true, 0xfd, 0x79, 0);

  zx_status_t status;

  EXPECT_EQ(brcmf_sdiod_vendor_control_rb(&sdio_dev, 0xf0, &status), 0xab);
  EXPECT_EQ(status, ZX_ERR_IO);
  EXPECT_EQ(brcmf_sdiod_vendor_control_rb(&sdio_dev, 0xf3, nullptr), 0x12);

  brcmf_sdiod_vendor_control_wb(&sdio_dev, 0xff, 0x55, nullptr);
  brcmf_sdiod_vendor_control_wb(&sdio_dev, 0xfd, 0x79, &status);
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);

  sdio1.VerifyAndClear();
}

TEST(Sdio, Transfer) {
  brcmf_sdio_dev sdio_dev = {};

  MockSdio sdio1;
  MockSdio sdio2;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();

  sdio1.ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x458ef43b, 0xd25d48bb, true, true));
  sdio2.ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x216977b9, 0x9a1d98ed, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x9da7a590, 0xdc8290a3, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0xecf0a024, 0x57d91422, true, true));

  EXPECT_OK(
      brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_1, 0x458ef43b, true, nullptr, 0xd25d48bb, false));
  EXPECT_OK(
      brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_2, 0x216977b9, true, nullptr, 0x9a1d98ed, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, 0, 0x9da7a590, true, nullptr, 0xdc8290a3, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, 200, 0xecf0a024, true, nullptr, 0x57d91422, false));

  sdio1.VerifyAndClear();
  sdio2.VerifyAndClear();
}

TEST(Sdio, DmaTransfer) {
  brcmf_sdio_dev sdio_dev = {};

  // In order to write data to the VMO we need an actual valid address, use some
  // test data.
  std::array<unsigned char, 4096> dma_test_data;

  sdio_dev.dma_buffer_size = 16384;
  ASSERT_OK(zx::vmo::create(sdio_dev.dma_buffer_size, ZX_VMO_RESIZABLE, &sdio_dev.dma_buffer));

  MockSdio sdio1;
  MockSdio sdio2;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();

  // These transactions should not have DMA enabled because they do not meet
  // the criteria.
  auto unaligned_size = MakeSdioTxn(0x458ef43b, 1233, true, true, false, ZX_HANDLE_INVALID);
  auto too_small_for_dma = MakeSdioTxn(0x216977b9, 16, true, true, false, ZX_HANDLE_INVALID);
  // These transactions should have DMA enabled
  auto perfect_for_dma =
      MakeSdioTxn(0x9da7a590, dma_test_data.size(), true, true, true, sdio_dev.dma_buffer.get());

  sdio1.ExpectDoRwTxn(ZX_OK, unaligned_size)
      .ExpectDoRwTxn(ZX_OK, too_small_for_dma)
      .ExpectDoRwTxn(ZX_OK, perfect_for_dma);

  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_1, 0x458ef43b, true, nullptr,
                                 unaligned_size.data_size, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_1, 0x216977b9, true, nullptr,
                                 too_small_for_dma.data_size, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_1, 0x9da7a590, true, dma_test_data.data(),
                                 dma_test_data.size(), false));

  sdio1.VerifyAndClear();

  sdio2.ExpectDoRwTxn(ZX_OK, unaligned_size)
      .ExpectDoRwTxn(ZX_OK, too_small_for_dma)
      .ExpectDoRwTxn(ZX_OK, perfect_for_dma);

  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_2, 0x458ef43b, true, nullptr,
                                 unaligned_size.data_size, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_2, 0x216977b9, true, nullptr,
                                 too_small_for_dma.data_size, false));
  EXPECT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_2, 0x9da7a590, true, dma_test_data.data(),
                                 dma_test_data.size(), false));

  sdio2.VerifyAndClear();

  // Data provided is a nullptr, ensure this is not OK when using DMA, that
  // data needs to be copied.
  EXPECT_NOT_OK(brcmf_sdiod_transfer(&sdio_dev, SDIO_FN_2, 0x9da7a590, true, nullptr,
                                     dma_test_data.size(), false));
  // And make sure there are no interactions with sdio
  sdio2.VerifyAndClear();
}

TEST(Sdio, IoAbort) {
  brcmf_sdio_dev sdio_dev = {};

  MockSdio sdio1;
  MockSdio sdio2;
  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.sdio_proto_fn2 = *sdio2.GetProto();

  sdio1.ExpectIoAbort(ZX_OK);
  sdio2.ExpectIoAbort(ZX_OK).ExpectIoAbort(ZX_OK).ExpectIoAbort(ZX_OK);

  EXPECT_OK(brcmf_sdiod_abort(&sdio_dev, 1));
  EXPECT_OK(brcmf_sdiod_abort(&sdio_dev, 2));
  EXPECT_OK(brcmf_sdiod_abort(&sdio_dev, 0));
  EXPECT_OK(brcmf_sdiod_abort(&sdio_dev, 200));

  sdio1.VerifyAndClear();
  sdio2.VerifyAndClear();
}

TEST(Sdio, RamRw) {
  brcmf_sdio_dev sdio_dev = {};
  sdio_func func1 = {};
  pthread_mutex_init(&func1.lock, nullptr);

  MockSdio sdio1;

  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.func1 = &func1;

  sdio1.ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x0000ffe0, 0x00000020, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x0001000a, 0x00000001, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x0001000b, 0x00000001, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x0001000c, 0x00000001, true, true))
      .ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x00008000, 0x00000020, true, true));

  /* In this test the address is set to 0x000007fe0, and when running, this function
   will chunk the data which is originally 0x40 bytes big into two pieces to align
   the next transfer address to SBSDIO_SB_OFT_ADDR_LIMIT, which is 0x8000, each one
   is 0x20 bytes big. The first line above corresponding to the first piece, and the
   fifth line is the second piece, middle three are txns made in
   brcmf_sdiod_set_backplane_window()
   */
  EXPECT_OK(brcmf_sdiod_ramrw(&sdio_dev, true, 0x00007fe0, nullptr, 0x00000040));
  sdio1.VerifyAndClear();
}

// This test case verifies that whether an error will returned when transfer size is
// not divisible by 4.
TEST(Sdio, AlignSize) {
  brcmf_sdio_dev sdio_dev = {};
  sdio_func func1 = {};
  pthread_mutex_init(&func1.lock, nullptr);

  MockSdio sdio1;

  sdio_dev.sdio_proto_fn1 = *sdio1.GetProto();
  sdio_dev.func1 = &func1;

  sdio1.ExpectDoRwTxn(ZX_OK, MakeSdioTxn(0x00008000, 0x00000020, true, true));

  // 4-byte-aligned size should succeed.
  EXPECT_OK(brcmf_sdiod_ramrw(&sdio_dev, true, 0x00000000, nullptr, 0x00000020));
  // non-4-byte-aligned size for sending should fail and return ZX_ERR_INVALID_ARGS.
  EXPECT_EQ(brcmf_sdiod_ramrw(&sdio_dev, true, 0x00000000, nullptr, 0x00000021),
            ZX_ERR_INVALID_ARGS);
  // non-4-byte-aligned size for receiving should fail and return ZX_ERR_INVALID_ARGS.
  EXPECT_EQ(brcmf_sdiod_ramrw(&sdio_dev, false, 0x00000000, nullptr, 0x00000021),
            ZX_ERR_INVALID_ARGS);
  sdio1.VerifyAndClear();
}

// This test case verifies the alignment functionality of pkt_align() defined in sdio.cc is correct.

TEST(Sdio, PktAlignTest) {
  for (size_t pkt_size = NOT_ALIGNED_SIZE; pkt_size <= ALIGNED_SIZE; pkt_size++) {
    struct brcmf_netbuf* buf = brcmf_netbuf_allocate(pkt_size);
    brcmf_netbuf_grow_tail(buf, pkt_size);
    // The third parameter is not used to do alignment for buf->len.
    pkt_align(buf, NOT_ALIGNED_SIZE, DMA_ALIGNMENT);
    // Check whether the memory position of data pointer is aligned.
    EXPECT_EQ((unsigned long)buf->data % DMA_ALIGNMENT, 0);
    // Check whether the "len" field in buf is aligned for SDIO transfer.
    EXPECT_EQ(buf->len, ALIGNED_SIZE);

    brcmf_netbuf_free(buf);
  }
}

/*
 * The sdio_bus_txctl_test() test helper calls brcmf_sdio_bus_txctl() by mocking the state around
 * the call using the function arguments. This helper returns the status returned by the
 * brcmf_sdio_bus_txctl() call.
 *
 *   sdiod_state       - state of the brcmf_sdio_dev contained in brcmf_bus
 *   ctl_done_timeout  - duration brcmf_sdio_bus_txctl() should wait for the
 *                       work_item_handler to complete before timing out
 *   workqueue_name    - name of the workqueue that effecively mocks dpc
 *   work_item_handler - WorkItem loaded into a WorkQueue to mock dpc
 *   expected_tx_ctlpkts - expected value of sdcnt.tx_ctlpkts after calling brcmf_sdio_bus_txctl()
 *   expected_tx_ctlerrs - expected value of sdcnt.tx_ctlerrs after calling brcmf_sdio_bus_txctl()
 *
 */
static zx_status_t sdio_bus_txctl_test(enum brcmf_sdiod_state sdiod_state,
                                       zx_duration_t ctl_done_timeout, const char* workqueue_name,
                                       void (*work_item_handler)(WorkItem* work),
                                       ulong expected_tx_ctlpkts, ulong expected_tx_ctlerrs) {
  // Minimal initialization of brcmf_sdio_dev, brcmf_bus, and brcmf_sdio required to call
  // brcmf_sdio_bus_txctl().
  brcmf_sdio_dev sdio_dev = {.ctl_done_timeout = ctl_done_timeout, .state = sdiod_state};

  sdio_func func1 = {};
  pthread_mutex_init(&func1.lock, nullptr);
  sdio_dev.func1 = &func1;

  struct brcmf_bus bus_if = {};
  bus_if.bus_priv.sdio = &sdio_dev;
  sdio_dev.bus_if = &bus_if;

  struct brcmf_sdio bus = {};
  sdio_dev.bus = &bus;
  bus.sdiodev = &sdio_dev;

  // Prepare a WorkQueue with a single WorkItem to run the work_item_handler when
  // brcmf_sdio_bus_txctl is called.
  WorkQueue wq = WorkQueue(workqueue_name);
  bus.brcmf_wq = &wq;
  bus.datawork = WorkItem(work_item_handler);
  bus.dpc_triggered.store(false);

  // Call brcmf_sdio_bus_txctl() with a blank message. Message processing is not mocked,
  // so this call purely tests
  unsigned char msg[] = "";
  uint msglen = strlen((char*)msg);
  zx_status_t status = brcmf_sdio_bus_txctl(&bus_if, msg, msglen);
  EXPECT_EQ(bus.sdcnt.tx_ctlpkts, expected_tx_ctlpkts);
  EXPECT_EQ(bus.sdcnt.tx_ctlerrs, expected_tx_ctlerrs);

  return status;
}

TEST(Sdio, TxCtlSdioDown) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DOWN, ZX_MSEC(CTL_DONE_TIMEOUT_MSEC), "brcmf_wq/txctl_sdio_down",
      [](WorkItem* work_item) {}, 0, 0);
  EXPECT_EQ(status, ZX_ERR_IO);
}

TEST(Sdio, TxCtlOk) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DATA, ZX_MSEC(CTL_DONE_TIMEOUT_MSEC), "brcmf_wq/txctl_ok",
      [](WorkItem* work) {
        struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);
        brcmf_sdio_if_ctrl_frame_stat_set(bus, [&bus]() {
          bus->ctrl_frame_err = ZX_OK;
          std::atomic_thread_fence(std::memory_order_seq_cst);
          brcmf_sdio_wait_event_wakeup(bus);
        });
      },
      1, 0);
  EXPECT_EQ(status, ZX_OK);
}

TEST(Sdio, TxCtlTimeout) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DATA, ZX_MSEC(1), "brcmf_wq/txctl_timeout", [](WorkItem* work) {}, 0, 1);
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);
}

TEST(Sdio, TxCtlTimeoutUnexpectedCtrlFrameStatClear) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DATA, ZX_MSEC(1), "brcmf_wq/txctl_timeout_unexpected_ctrl_frame_stat_clear",
      [](WorkItem* work) {
        struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);
        brcmf_sdio_if_ctrl_frame_stat_set(bus, [&bus]() { bus->ctrl_frame_err = ZX_OK; });
      },
      0, 1);
  EXPECT_EQ(status, ZX_ERR_TIMED_OUT);
}

TEST(Sdio, TxCtlCtrlFrameStateNotCleared) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DATA, ZX_MSEC(CTL_DONE_TIMEOUT_MSEC),
      "brcmf_wq/txctl_ctrl_frame_stat_not_cleared",
      [](WorkItem* work) {
        struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        brcmf_sdio_wait_event_wakeup(bus);
      },
      0, 1);
  EXPECT_EQ(status, ZX_ERR_SHOULD_WAIT);
}

TEST(Sdio, TxCtlCtrlFrameStateClearedWithError) {
  zx_status_t status = sdio_bus_txctl_test(
      BRCMF_SDIOD_DATA, ZX_MSEC(CTL_DONE_TIMEOUT_MSEC),
      "brcmf_wq/txctl_ctrl_frame_stat_cleared_with_error",
      [](WorkItem* work) {
        struct brcmf_sdio* bus = containerof(work, struct brcmf_sdio, datawork);
        brcmf_sdio_if_ctrl_frame_stat_set(bus, [&bus]() {
          bus->ctrl_frame_err = ZX_ERR_NO_MEMORY;
          std::atomic_thread_fence(std::memory_order_seq_cst);
          brcmf_sdio_wait_event_wakeup(bus);
        });
      },
      1, 0);
  EXPECT_EQ(status, ZX_ERR_NO_MEMORY);
}
}  // namespace
