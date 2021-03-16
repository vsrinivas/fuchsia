// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-transaction.h"

#include <lib/ddk/debug.h>

#include <fbl/auto_lock.h>
#include <soc/mt8167/mt8167-usb.h>

namespace mt_usb_hci {

namespace regs = board_mt8167;

namespace {

// Read up to len bytes from the given endpoint-FIFO and write it to the output buffer.
size_t FifoRead(uint8_t ep, void* out, size_t len, ddk::MmioBuffer* usb) {
  // It's possible the device returned less data than was requested.  This is not an error.
  auto actual = static_cast<size_t>(regs::RXCOUNT::Get(ep).ReadFrom(usb).rxcount());
  size_t remaining = (actual < len) ? actual : len;

  auto dest = static_cast<uint32_t*>(out);
  while (remaining >= 4) {
    *dest++ = regs::FIFO::Get(ep).ReadFrom(usb).fifo_data();
    remaining -= 4;
  }

  auto dest_8 = reinterpret_cast<uint8_t*>(dest);
  while (remaining > 0) {
    *dest_8++ = regs::FIFO_8::Get(ep).ReadFrom(usb).fifo_data();
    remaining--;
  }
  return actual;
}

// Write len bytes from the input buffer to the given endpoint-FIFO.
size_t FifoWrite(uint8_t ep, const void* in, size_t len, ddk::MmioBuffer* usb) {
  auto remaining = len;
  auto src = static_cast<const uint8_t*>(in);
  auto fifo = regs::FIFO_8::Get(ep).FromValue(0);
  while (remaining-- > 0) {
    fifo.set_fifo_data(*src++).WriteTo(usb);
  }
  return len;
}

}  // namespace

void Control::AbortAs(ControlState state) {
  // To abort, flush the EP0-FIFO and clear all error-bits.
  regs::CSR0_HOST::Get()
      .ReadFrom(&usb_)
      .set_flushfifo(1)
      .set_error(0)
      .set_naktimeout(0)
      .set_rxstall(0)
      .WriteTo(&usb_);
  state_ = state;
}

bool Control::BusError() {
  // TODO(hansens) implement proper control NAK-retry logic.
  auto reg = regs::CSR0_HOST::Get().ReadFrom(&usb_);
  if (reg.error())
    zxlogf(ERROR, "usb device error");
  if (reg.naktimeout())
    zxlogf(ERROR, "usb device naktimeout");
  if (reg.rxstall())
    zxlogf(ERROR, "usb device rxstall");
  return reg.error() || reg.naktimeout() || reg.rxstall();
}

void Control::Advance(bool interrupt) {
  fbl::AutoLock _(&lock_);
  // clang-format off
    while (!terminal_ && (interrupt || !irq_wait_.load())) {
        interrupt = false;
        switch (state_) {
        case ControlState::SETUP:          AdvanceSetup(); break;
        case ControlState::SETUP_IRQ:      AdvanceSetupIrq(); break;
        case ControlState::IN_DATA:        AdvanceInData(); break;
        case ControlState::IN_DATA_IRQ:    AdvanceInDataIrq(); break;
        case ControlState::OUT_DATA:       AdvanceOutData(); break;
        case ControlState::OUT_DATA_IRQ:   AdvanceOutDataIrq(); break;
        case ControlState::IN_STATUS:      AdvanceInStatus(); break;
        case ControlState::IN_STATUS_IRQ:  AdvanceInStatusIrq(); break;
        case ControlState::OUT_STATUS:     AdvanceOutStatus(); break;
        case ControlState::OUT_STATUS_IRQ: AdvanceOutStatusIrq(); break;
        case ControlState::SUCCESS:        AdvanceSuccess(); break;
        case ControlState::ERROR:          AdvanceError(); break;
        case ControlState::CANCEL:         AdvanceCancel(); break;
        }
    }
  // clang-format on
}

void Control::Cancel() {
  {
    fbl::AutoLock _(&lock_);
    if (state_.load() < ControlState::SUCCESS) {  // Non-terminal.
      irq_wait_ = false;
      state_ = ControlState::CANCEL;
    }
  }
  Advance();
}

void Control::AdvanceSetup() {
  FifoWrite(0, &req_, sizeof(req_), &usb_);
  regs::TXFUNCADDR::Get(0).FromValue(0).set_tx_func_addr(faddr_).WriteTo(&usb_);
  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_setuppkt(1).set_txpktrdy(1).set_disping(1).WriteTo(
      &usb_);

  state_ = ControlState::SETUP_IRQ;
  irq_wait_ = true;
}

void Control::AdvanceSetupIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(ControlState::ERROR);
    return;
  }

  // clang-format off
    switch (type_) {
    case ControlType::ZERO:  state_ = ControlState::IN_STATUS; break;
    case ControlType::READ:  state_ = ControlState::IN_DATA; break;
    case ControlType::WRITE: state_ = ControlState::OUT_DATA; break;
    }
  // clang-format on
}

void Control::AdvanceInData() {
  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_reqpkt(1).WriteTo(&usb_);
  state_ = ControlState::IN_DATA_IRQ;
  irq_wait_ = true;
}

void Control::AdvanceInDataIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(ControlState::ERROR);
    return;
  }
  size_t remaining = len_ - actual_.load();
  auto offset = reinterpret_cast<uintptr_t>(buffer_) + actual_.load();
  auto read = FifoRead(0, reinterpret_cast<void*>(offset), remaining, &usb_);

  actual_ += read;
  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_rxpktrdy(0).WriteTo(&usb_);

  if (read < max_pkt_sz0_) {
    // The device transferred a short packet signifying end of transmission.
    state_ = ControlState::OUT_STATUS;
  } else {
    state_ = (actual_.load() < len_) ? ControlState::IN_DATA : ControlState::OUT_STATUS;
  }
}

void Control::AdvanceOutData() {
  // Write at most one packet's worth of data to the device.
  size_t remaining = len_ - actual_.load();
  size_t len = (remaining > max_pkt_sz0_) ? max_pkt_sz0_ : remaining;
  auto offset = reinterpret_cast<uintptr_t>(buffer_) + actual_.load();
  actual_ += FifoWrite(0, reinterpret_cast<void*>(offset), len, &usb_);
  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_txpktrdy(1).set_disping(1).WriteTo(&usb_);

  state_ = ControlState::OUT_DATA_IRQ;
  irq_wait_ = true;
}

void Control::AdvanceOutDataIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(ControlState::ERROR);
    return;
  }
  state_ = (actual_.load() < len_) ? ControlState::OUT_DATA : ControlState::IN_STATUS;
}

void Control::AdvanceInStatus() {
  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_statuspkt(1).set_reqpkt(1).WriteTo(&usb_);

  state_ = ControlState::IN_STATUS_IRQ;
  irq_wait_ = true;
}

void Control::AdvanceInStatusIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(ControlState::ERROR);
    return;
  }

  regs::CSR0_HOST::Get().ReadFrom(&usb_).set_statuspkt(0).set_rxpktrdy(0).WriteTo(&usb_);

  state_ = ControlState::SUCCESS;
}

void Control::AdvanceOutStatus() {
  regs::CSR0_HOST::Get().FromValue(0).set_statuspkt(1).set_txpktrdy(1).set_disping(1).WriteTo(
      &usb_);

  state_ = ControlState::OUT_STATUS_IRQ;
  irq_wait_ = true;
}

void Control::AdvanceOutStatusIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(ControlState::ERROR);
    return;
  }
  state_ = ControlState::SUCCESS;
}

void Control::AdvanceSuccess() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void Control::AdvanceError() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void Control::AdvanceCancel() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void BulkBase::AbortAs(BulkState state) {
  // To abort, flush the endpoint-FIFO and clear all error-bits.
  if (dir_ == BulkDirection::IN) {
    auto csr = regs::RXCSR_HOST::Get(ep_).ReadFrom(&usb_);
    if (csr.rxpktrdy()) {
      csr.set_flushfifo(1).WriteTo(&usb_);
    }
    regs::RXCSR_HOST::Get(ep_)
        .ReadFrom(&usb_)
        .set_error(0)
        .set_dataerr_naktimeout(0)
        .set_rxstall(0)
        .WriteTo(&usb_);
  } else {
    auto csr = regs::TXCSR_HOST::Get(ep_).ReadFrom(&usb_);
    if (csr.txpktrdy()) {
      csr.set_flushfifo(1).WriteTo(&usb_);
    }
    regs::TXCSR_HOST::Get(ep_)
        .ReadFrom(&usb_)
        .set_flushfifo(1)
        .set_error(0)
        .set_naktimeout_incomptx(0)
        .set_rxstall(0)
        .WriteTo(&usb_);
  }
  state_ = state;
}

bool BulkBase::BusError() {
  bool ret;
  if (dir_ == BulkDirection::IN) {
    auto reg = regs::RXCSR_HOST::Get(ep_).ReadFrom(&usb_);
    if (reg.error())
      zxlogf(ERROR, "usb device RX error");
    if (reg.dataerr_naktimeout())
      zxlogf(ERROR, "usb device RX naktimeout");
    if (reg.rxstall())
      zxlogf(ERROR, "usb device RX rxstall");
    ret = reg.error() || reg.dataerr_naktimeout() || reg.rxstall();
  } else {
    auto reg = regs::TXCSR_HOST::Get(ep_).ReadFrom(&usb_);
    if (reg.error())
      zxlogf(ERROR, "usb device TX error");
    if (reg.naktimeout_incomptx())
      zxlogf(ERROR, "usb device TX naktimeout");
    if (reg.rxstall())
      zxlogf(ERROR, "usb device TX rxstall");
    ret = reg.error() || reg.naktimeout_incomptx() || reg.rxstall();
  }
  return ret;
}

void BulkBase::Advance(bool interrupt) {
  fbl::AutoLock _(&lock_);
  // clang-format off
    while (!terminal_ && (interrupt || !irq_wait_.load())) {
        interrupt = false;
        switch (state_) {
        case BulkState::SETUP:     AdvanceSetup(); break;
        case BulkState::SETUP_IN:  AdvanceSetupIn(); break;
        case BulkState::SETUP_OUT: AdvanceSetupOut(); break;
        case BulkState::SEND:      AdvanceSend(); break;
        case BulkState::SEND_IRQ:  AdvanceSendIrq(); break;
        case BulkState::RECV:      AdvanceRecv(); break;
        case BulkState::RECV_IRQ:  AdvanceRecvIrq(); break;
        case BulkState::SUCCESS:   AdvanceSuccess(); break;
        case BulkState::ERROR:     AdvanceError(); break;
        case BulkState::CANCEL:    AdvanceCancel(); break;
        }
    }
  // clang-format on
}

void BulkBase::Cancel() {
  {
    fbl::AutoLock _(&lock_);
    if (state_.load() < BulkState::SUCCESS) {  // Non-terminal.
      irq_wait_ = false;
      state_ = BulkState::CANCEL;
    }
  }
  Advance();
}

void BulkBase::AdvanceSetup() {
  state_ = (dir_ == BulkDirection::IN) ? BulkState::SETUP_IN : BulkState::SETUP_OUT;
}

void BulkBase::AdvanceSend() {
  // Transmit at most one packet's worth of data to the device.
  size_t remaining = len_ - actual_.load();
  size_t xfer_len = (remaining > max_pkt_sz_) ? max_pkt_sz_ : remaining;
  auto offset = reinterpret_cast<uintptr_t>(buffer_) + actual_.load();
  size_t written = FifoWrite(ep_, reinterpret_cast<void*>(offset), xfer_len, &usb_);
  pkt_aligned_ = written == max_pkt_sz_;
  actual_ += written;

  regs::TXCSR_HOST::Get(ep_).ReadFrom(&usb_).set_txpktrdy(1).WriteTo(&usb_);
  state_ = BulkState::SEND_IRQ;
  irq_wait_ = true;
}

void BulkBase::AdvanceSendIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(BulkState::ERROR);
    return;
  }

  // Here, it's possible that the last chunk of data sent was exactly one packet in size.  In this
  // case, the receiving device may still be awaiting data.  We need to send a short-packet
  // (zero-length in this case) to tell the receiver we are done transmitting.  This zero-length
  // packet case is identified by actual_ == len_ and pkt_aligned_ == true.
  state_ = (actual_.load() < len_ || pkt_aligned_) ? BulkState::SEND : BulkState::SUCCESS;
}

void BulkBase::AdvanceRecv() {
  regs::RXCSR_HOST::Get(ep_).FromValue(0).set_reqpkt(1).WriteTo(&usb_);

  state_ = BulkState::RECV_IRQ;
  irq_wait_ = true;
}

void BulkBase::AdvanceRecvIrq() {
  irq_wait_ = false;
  if (BusError()) {
    AbortAs(BulkState::ERROR);
    return;
  }

  size_t remaining = len_ - actual_.load();
  auto offset = reinterpret_cast<uintptr_t>(buffer_) + actual_.load();
  size_t read = FifoRead(ep_, reinterpret_cast<void*>(offset), remaining, &usb_);
  pkt_aligned_ = read == max_pkt_sz_;
  actual_ += read;

  regs::RXCSR_HOST::Get(ep_).ReadFrom(&usb_).set_rxpktrdy(0).WriteTo(&usb_);

  // A short read indicates the device is done transmitting data.
  if (read < max_pkt_sz_) {
    // The device transferred a short packet signifying end of transmission.
    state_ = BulkState::SUCCESS;
  } else {
    state_ = (actual_.load() < len_) ? BulkState::RECV : BulkState::SUCCESS;
  }
}

void BulkBase::AdvanceSuccess() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void BulkBase::AdvanceError() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void BulkBase::AdvanceCancel() {
  terminal_ = true;
  sync_completion_signal(&complete_);
}

void Bulk::AdvanceSetupIn() {
  regs::RXFUNCADDR::Get(ep_).FromValue(0).set_rx_func_addr(faddr_).WriteTo(&usb_);
  regs::RXINTERVAL::Get(ep_).FromValue(0).set_rx_polling_interval_nak_limit_m(interval_).WriteTo(
      &usb_);
  regs::RXTYPE::Get(ep_)
      .FromValue(0)
      .set_rx_protocol(0x2)  // Bulk-type.
      .set_rx_target_ep_number(ep_)
      .WriteTo(&usb_);
  regs::RXMAP::Get(ep_)
      .ReadFrom(&usb_)
      .set_maximum_payload_transaction(static_cast<uint16_t>(max_pkt_sz_))
      .WriteTo(&usb_);

  // If double-buffering is enabled, we need to flush the RX-FIFO twice, see: MUSBMHDRC 22.2.1.1.
  auto csr = regs::RXCSR_HOST::Get(ep_).ReadFrom(&usb_);
  if (csr.rxpktrdy()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }
  if (csr.rxpktrdy()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }

  state_ = BulkState::RECV;
}

void Bulk::AdvanceSetupOut() {
  regs::TXFUNCADDR::Get(ep_).FromValue(0).set_tx_func_addr(faddr_).WriteTo(&usb_);
  regs::TXINTERVAL::Get(ep_).FromValue(0).set_tx_polling_interval_nak_limit_m(interval_).WriteTo(
      &usb_);
  regs::TXTYPE::Get(ep_)
      .FromValue(0)
      .set_tx_protocol(0x2)  // Bulk-type.
      .set_tx_target_ep_number(ep_)
      .WriteTo(&usb_);
  regs::TXMAP::Get(ep_)
      .FromValue(0)
      .set_maximum_payload_transaction(static_cast<uint16_t>(max_pkt_sz_))
      .WriteTo(&usb_);

  // If double-buffering is enabled, we need to flush the TX-FIFO twice, see: MUSBMHDRC 22.2.2.1.
  auto csr = regs::TXCSR_HOST::Get(ep_).ReadFrom(&usb_);
  if (csr.fifonotempty()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }
  if (csr.fifonotempty()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }

  state_ = BulkState::SEND;
}

void Interrupt::AdvanceSetupIn() {
  regs::RXFUNCADDR::Get(ep_).FromValue(0).set_rx_func_addr(faddr_).WriteTo(&usb_);
  regs::RXINTERVAL::Get(ep_).FromValue(0).set_rx_polling_interval_nak_limit_m(interval_).WriteTo(
      &usb_);
  regs::RXTYPE::Get(ep_)
      .FromValue(0)
      .set_rx_protocol(0x3)  // Interrupt-type.
      .set_rx_target_ep_number(ep_)
      .WriteTo(&usb_);
  regs::RXMAP::Get(ep_)
      .FromValue(0)
      .set_maximum_payload_transaction(static_cast<uint16_t>(max_pkt_sz_))
      .WriteTo(&usb_);

  // If double-buffering is enabled, we need to flush the RX-FIFO twice, see: MUSBMHDRC 22.2.1.1.
  auto csr = regs::RXCSR_HOST::Get(ep_).ReadFrom(&usb_);
  if (csr.rxpktrdy()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }
  if (csr.rxpktrdy()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }

  state_ = BulkState::RECV;
}

void Interrupt::AdvanceSetupOut() {
  regs::TXFUNCADDR::Get(ep_).FromValue(0).set_tx_func_addr(faddr_).WriteTo(&usb_);
  regs::TXINTERVAL::Get(ep_).FromValue(0).set_tx_polling_interval_nak_limit_m(interval_).WriteTo(
      &usb_);
  regs::TXTYPE::Get(ep_)
      .FromValue(0)
      .set_tx_protocol(0x3)  // Interrupt-type.
      .set_tx_target_ep_number(ep_)
      .WriteTo(&usb_);
  regs::TXMAP::Get(ep_)
      .FromValue(0)
      .set_maximum_payload_transaction(static_cast<uint16_t>(max_pkt_sz_))
      .WriteTo(&usb_);

  // If double-buffering is enabled, we need to flush the TX-FIFO twice, see: MUSBMHDRC 22.2.2.1.
  auto csr = regs::TXCSR_HOST::Get(ep_).ReadFrom(&usb_);
  if (csr.fifonotempty()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }
  if (csr.fifonotempty()) {
    csr.set_flushfifo(1).WriteTo(&usb_);
  }

  state_ = BulkState::SEND;
}

}  // namespace mt_usb_hci
