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

// This is required to use ddk::MockSdio.
bool operator==(const sdio_rw_txn_t& lhs, const sdio_rw_txn_t& rhs) {
  return (lhs.addr == rhs.addr && lhs.data_size == rhs.data_size && lhs.incr == rhs.incr &&
          lhs.write == rhs.write && lhs.buf_offset == rhs.buf_offset);
}

zx_status_t get_wifi_metadata(struct brcmf_bus* bus, void* data, size_t exp_size, size_t* actual) {
  return device_get_metadata(bus->bus_priv.sdio->drvr->zxdev, DEVICE_METADATA_WIFI_CONFIG, data,
                             exp_size, actual);
}

namespace {

constexpr sdio_rw_txn MakeSdioTxn(uint32_t addr, uint32_t data_size, bool incr, bool write) {
  return {.addr = addr,
          .data_size = data_size,
          .incr = incr,
          .write = write,
          .use_dma = false,
          .dma_vmo = ZX_HANDLE_INVALID,
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

  EXPECT_OK(brcmf_sdiod_write(&sdio_dev, SDIO_FN_1, 0x458ef43b, nullptr, 0xd25d48bb));
  EXPECT_OK(brcmf_sdiod_write(&sdio_dev, SDIO_FN_2, 0x216977b9, nullptr, 0x9a1d98ed));
  EXPECT_OK(brcmf_sdiod_write(&sdio_dev, 0, 0x9da7a590, nullptr, 0xdc8290a3));
  EXPECT_OK(brcmf_sdiod_write(&sdio_dev, 200, 0xecf0a024, nullptr, 0x57d91422));

  sdio1.VerifyAndClear();
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
  EXPECT_OK(brcmf_sdiod_ramrw(&sdio_dev, true, 0x000007fe0, nullptr, 0x00000040));
  sdio1.VerifyAndClear();
}

}  // namespace
