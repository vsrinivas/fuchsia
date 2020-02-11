// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-transaction.h"

#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <memory>
#include <optional>
#include <soc/mt8167/mt8167-usb.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

namespace mt_usb_hci {
namespace regs = board_mt8167;

class ControlTest : public zxtest::Test {
 protected:
  void SetUp() {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(0x731, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, 0x731, std::move(vmo), ZX_CACHE_POLICY_CACHED, &usb_));
  }

  std::optional<ddk::MmioBuffer> usb_;
};

TEST_F(ControlTest, TestZeroSuccess) {
  usb_setup_t req;
  uint8_t buf[8];
  size_t max_pkt_sz = 64;
  ddk::MmioView v = usb_->View(0);

  Control ctl(ControlType::ZERO, v.View(0), req, &buf, sizeof(buf), max_pkt_sz, 123);

  ctl.Advance();  // SETUP -> irq wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(0).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).setuppkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).txpktrdy());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).disping());
  EXPECT_EQ(ControlState::SETUP_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> SETUP_IRQ -> IN_STATUS -> irq wait.
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).statuspkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).reqpkt());
  EXPECT_EQ(ControlState::IN_STATUS_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> IN_STATUS_IRQ -> SUCCESS.
  EXPECT_EQ(0, regs::CSR0_HOST::Get().ReadFrom(&v).statuspkt());
  EXPECT_EQ(0, regs::CSR0_HOST::Get().ReadFrom(&v).rxpktrdy());
  EXPECT_EQ(ControlState::SUCCESS, ctl.state());
  EXPECT_TRUE(ctl.Ok());
}

TEST_F(ControlTest, TestReadSuccess) {
  usb_setup_t req;
  uint8_t buf[8];
  size_t max_pkt_sz = 64;
  ddk::MmioView v = usb_->View(0);

  Control ctl(ControlType::READ, v.View(0), req, &buf, sizeof(buf), max_pkt_sz, 123);

  ctl.Advance();  // SETUP -> irq wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(0).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).setuppkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).txpktrdy());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).disping());
  EXPECT_EQ(ControlState::SETUP_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> SETUP_IRQ -> IN_DATA -> irq wait.
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).reqpkt());
  EXPECT_EQ(ControlState::IN_DATA_IRQ, ctl.state());

  regs::RXCOUNT::Get(0).FromValue(0).set_rxcount(sizeof(buf)).WriteTo(&v);
  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> IN_DATA_IRQ -> OUT_STATUS -> irq wait.
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).statuspkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).txpktrdy());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).disping());
  EXPECT_EQ(ControlState::OUT_STATUS_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> OUT_STATUS_IRQ -> SUCCESS.
  EXPECT_EQ(ControlState::SUCCESS, ctl.state());
  EXPECT_TRUE(ctl.Ok());
}

TEST_F(ControlTest, TestWriteSuccess) {
  usb_setup_t req;
  uint8_t buf[8];
  size_t max_pkt_sz = 64;
  ddk::MmioView v = usb_->View(0);

  Control ctl(ControlType::WRITE, v.View(0), req, &buf, sizeof(buf), max_pkt_sz, 123);

  ctl.Advance();  // SETUP -> irq wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(0).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).setuppkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).txpktrdy());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).disping());
  EXPECT_EQ(ControlState::SETUP_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> SETUP_IRQ -> OUT_DATA -> irq wait.
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).txpktrdy());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).disping());
  EXPECT_EQ(ControlState::OUT_DATA_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> OUT_DATA_IRQ -> IN_STATUS -> irq wait.
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).statuspkt());
  EXPECT_EQ(1, regs::CSR0_HOST::Get().ReadFrom(&v).reqpkt());
  EXPECT_EQ(ControlState::IN_STATUS_IRQ, ctl.state());

  regs::CSR0_HOST::Get().FromValue(0).WriteTo(&v);
  ctl.Advance(true);  // irq wait -> IN_STATUS_IRQ -> SUCCESS.
  EXPECT_EQ(ControlState::SUCCESS, ctl.state());
  EXPECT_TRUE(ctl.Ok());
}

TEST_F(ControlTest, TestControlCancel) {
  usb_setup_t req;
  uint8_t buf[8];
  size_t max_pkt_sz = 64;
  ddk::MmioView v = usb_->View(0);

  Control ctl(ControlType::ZERO, v.View(0), req, &buf, sizeof(buf), max_pkt_sz, 123);

  ctl.Advance();
  ctl.Cancel();
  EXPECT_EQ(ControlState::CANCEL, ctl.state());
}

class BulkTest : public zxtest::Test {
 protected:
  void SetUp() {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(0x731, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, 0x731, std::move(vmo), ZX_CACHE_POLICY_CACHED, &usb_));
  }

  std::optional<ddk::MmioBuffer> usb_;

  static constexpr usb_endpoint_descriptor_t in_descriptor_ = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = 0x81,
      .bmAttributes = 0x2,
      .wMaxPacketSize = 512,
      .bInterval = 255,
  };

  static constexpr usb_endpoint_descriptor_t out_descriptor_ = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = 0x2,
      .bmAttributes = 0x2,
      .wMaxPacketSize = 512,
      .bInterval = 255,
  };
};

TEST_F(BulkTest, TestReadSuccess) {
  uint8_t buf[1023];  // two packets of 512 and 511 bytes.
  uint8_t ep = 1;
  ddk::MmioView v = usb_->View(0);

  Bulk blk(v.View(0), 123, &buf, sizeof(buf), in_descriptor_);

  blk.Advance();  // SETUP -> SETUP_IN -> RECV -> irq_wait.
  EXPECT_EQ(123, regs::RXFUNCADDR::Get(ep).ReadFrom(&v).rx_func_addr());
  EXPECT_EQ(255, regs::RXINTERVAL::Get(ep).ReadFrom(&v).rx_polling_interval_nak_limit_m());
  EXPECT_EQ(2, regs::RXTYPE::Get(ep).ReadFrom(&v).rx_protocol());
  EXPECT_EQ(ep, regs::RXTYPE::Get(ep).ReadFrom(&v).rx_target_ep_number());
  EXPECT_EQ(512, regs::RXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::RXCSR_HOST::Get(ep).ReadFrom(&v).reqpkt());
  EXPECT_EQ(BulkState::RECV_IRQ, blk.state());

  // First bulk read (512 bytes).
  regs::RXCOUNT::Get(ep).FromValue(ep).set_rxcount(512).WriteTo(&v);
  regs::RXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> RECV_IRQ -> RECV -> irq wait.
  EXPECT_EQ(1, regs::RXCSR_HOST::Get(ep).ReadFrom(&v).reqpkt());
  EXPECT_EQ(BulkState::RECV_IRQ, blk.state());

  // Second bulk read (511 bytes).
  regs::RXCOUNT::Get(ep).FromValue(ep).set_rxcount(511).WriteTo(&v);
  regs::RXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> RECV_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, blk.state());
  EXPECT_TRUE(blk.Ok());
}

TEST_F(BulkTest, TestWriteSuccess) {
  uint8_t buf[1023];  // two packets of 512 and 511 bytes.
  uint8_t ep = 2;
  ddk::MmioView v = usb_->View(0);

  Bulk blk(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  blk.Advance();  // SETUP -> SETUP_OUT -> SEND -> irq_wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(ep).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(255, regs::TXINTERVAL::Get(ep).ReadFrom(&v).tx_polling_interval_nak_limit_m());
  EXPECT_EQ(2, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_protocol());
  EXPECT_EQ(ep, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_target_ep_number());
  EXPECT_EQ(512, regs::TXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, blk.state());

  // First bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, blk.state());

  // Second bulk write (511 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> SEND_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, blk.state());
  EXPECT_TRUE(blk.Ok());
}

TEST_F(BulkTest, TestWriteSuccess_ZLP) {
  uint8_t buf[1024];  // two packets of 512 bytes plus a zero-length packet.
  uint8_t ep = 2;
  ddk::MmioView v = usb_->View(0);

  Bulk blk(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  blk.Advance();  // SETUP -> SETUP_OUT -> SEND -> irq_wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(ep).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(255, regs::TXINTERVAL::Get(ep).ReadFrom(&v).tx_polling_interval_nak_limit_m());
  EXPECT_EQ(2, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_protocol());
  EXPECT_EQ(ep, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_target_ep_number());
  EXPECT_EQ(512, regs::TXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, blk.state());

  // First bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, blk.state());

  // Second bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, blk.state());

  // Third bulk write (zlp).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  blk.Advance(true);  // irq_wait -> SEND_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, blk.state());
  EXPECT_TRUE(blk.Ok());
}

TEST_F(BulkTest, TestBulkCancel) {
  uint8_t buf[1023];
  ddk::MmioView v = usb_->View(0);

  Bulk blk(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  blk.Advance();
  blk.Cancel();
  EXPECT_EQ(BulkState::CANCEL, blk.state());
}

class InterruptTest : public zxtest::Test {
 protected:
  void SetUp() {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(0x731, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, 0x731, std::move(vmo), ZX_CACHE_POLICY_CACHED, &usb_));
  }

  std::optional<ddk::MmioBuffer> usb_;

  static constexpr usb_endpoint_descriptor_t in_descriptor_ = {
      sizeof(usb_endpoint_descriptor_t),  // .bLength
      USB_DT_ENDPOINT,                    // .bDescriptorType
      0x81,                               // .bEndpointAddress (ep=1, dir=in)
      0x3,                                // .bmAttributes (type=interrupt)
      512,                                // .wMaxPacketSize
      16,                                 // .bInterval
  };

  static constexpr usb_endpoint_descriptor_t out_descriptor_ = {
      sizeof(usb_endpoint_descriptor_t),  // .bLength
      USB_DT_ENDPOINT,                    // .bDescriptorType
      0x2,                                // .bEndpointAddress (ep=2, dir=out)
      0x3,                                // .bmAttributes (type=interrupt)
      512,                                // .wMaxPacketSize
      16,                                 // .bInterval
  };
};

TEST_F(InterruptTest, TestReadSuccess) {
  uint8_t buf[1023];  // two packets of 512 and 511 bytes.
  uint8_t ep = 1;
  ddk::MmioView v = usb_->View(0);

  Interrupt itr(v.View(0), 123, &buf, sizeof(buf), in_descriptor_);

  itr.Advance();  // SETUP -> SETUP_IN -> RECV -> irq_wait.
  EXPECT_EQ(123, regs::RXFUNCADDR::Get(ep).ReadFrom(&v).rx_func_addr());
  EXPECT_EQ(16, regs::RXINTERVAL::Get(ep).ReadFrom(&v).rx_polling_interval_nak_limit_m());
  EXPECT_EQ(3, regs::RXTYPE::Get(ep).ReadFrom(&v).rx_protocol());
  EXPECT_EQ(ep, regs::RXTYPE::Get(ep).ReadFrom(&v).rx_target_ep_number());
  EXPECT_EQ(512, regs::RXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::RXCSR_HOST::Get(ep).ReadFrom(&v).reqpkt());
  EXPECT_EQ(BulkState::RECV_IRQ, itr.state());

  // First bulk read (512 bytes).
  regs::RXCOUNT::Get(ep).FromValue(ep).set_rxcount(512).WriteTo(&v);
  regs::RXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> RECV_IRQ -> RECV -> irq wait.
  EXPECT_EQ(1, regs::RXCSR_HOST::Get(ep).ReadFrom(&v).reqpkt());
  EXPECT_EQ(BulkState::RECV_IRQ, itr.state());

  // Second bulk read (511 bytes).
  regs::RXCOUNT::Get(ep).FromValue(ep).set_rxcount(511).WriteTo(&v);
  regs::RXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> RECV_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, itr.state());
  EXPECT_TRUE(itr.Ok());
}

TEST_F(InterruptTest, TestWriteSuccess) {
  uint8_t buf[1023];  // two packets of 512 and 511 bytes.
  uint8_t ep = 2;
  ddk::MmioView v = usb_->View(0);

  Interrupt itr(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  itr.Advance();  // SETUP -> SETUP_OUT -> SEND -> irq_wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(ep).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(16, regs::TXINTERVAL::Get(ep).ReadFrom(&v).tx_polling_interval_nak_limit_m());
  EXPECT_EQ(3, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_protocol());
  EXPECT_EQ(ep, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_target_ep_number());
  EXPECT_EQ(512, regs::TXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, itr.state());

  // First bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, itr.state());

  // Second bulk write (511 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> SEND_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, itr.state());
  EXPECT_TRUE(itr.Ok());
}

TEST_F(InterruptTest, TestWriteSuccess_ZLP) {
  uint8_t buf[1024];  // two packets of 512 bytes plus a zero-length packet.
  uint8_t ep = 2;
  ddk::MmioView v = usb_->View(0);

  Interrupt itr(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  itr.Advance();  // SETUP -> SETUP_OUT -> SEND -> irq_wait.
  EXPECT_EQ(123, regs::TXFUNCADDR::Get(ep).ReadFrom(&v).tx_func_addr());
  EXPECT_EQ(16, regs::TXINTERVAL::Get(ep).ReadFrom(&v).tx_polling_interval_nak_limit_m());
  EXPECT_EQ(3, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_protocol());
  EXPECT_EQ(ep, regs::TXTYPE::Get(ep).ReadFrom(&v).tx_target_ep_number());
  EXPECT_EQ(512, regs::TXMAP::Get(ep).ReadFrom(&v).maximum_payload_transaction());
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, itr.state());

  // First bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, itr.state());

  // Second bulk write (512 bytes).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> SEND_IRQ -> SEND -> irq wait.
  EXPECT_EQ(1, regs::TXCSR_HOST::Get(ep).ReadFrom(&v).txpktrdy());
  EXPECT_EQ(BulkState::SEND_IRQ, itr.state());

  // Third bulk write (zlp).
  regs::TXCSR_HOST::Get(ep).FromValue(0).WriteTo(&v);
  itr.Advance(true);  // irq_wait -> SEND_IRQ -> SUCCESS.
  EXPECT_EQ(BulkState::SUCCESS, itr.state());
  EXPECT_TRUE(itr.Ok());
}

TEST_F(InterruptTest, TestInterruptCancel) {
  uint8_t buf[1023];
  ddk::MmioView v = usb_->View(0);

  Interrupt itr(v.View(0), 123, &buf, sizeof(buf), out_descriptor_);

  itr.Advance();
  itr.Cancel();
  EXPECT_EQ(BulkState::CANCEL, itr.state());
}

}  // namespace mt_usb_hci

int main(int argc, char* argv[]) { return RUN_ALL_TESTS(argc, argv); }
