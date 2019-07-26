// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <fbl/mutex.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>
#include <lib/zircon-internal/thread_annotations.h>

namespace mt_usb_hci {

// Transactions are implemented by a state machine whose internal state is advanced through
// subsequent calls to Advance().  Upon being advanced, state machines execute until a hardware
// interrupt is required to further progress, or they enter a terminal state.
class Transaction {
 public:
  virtual ~Transaction() = default;

  // Return the actual number of bytes processed by this transaction.
  virtual size_t actual() const = 0;

  // Advance the transaction machine and return when one of the following is true:
  //   1. The machine is awaiting a hardware IRQ.
  //   2. The machine is in a terminal state.
  // If the machine is awaiting a hardware interrupt, a call to Advance with interrupt=true must
  // be made to further advance the machine's state.  This interrupt call must be made as a result
  // of receiving an endpoint interrupt corresponding to this transaction.
  //
  // If the machine is awaiting a hardware interrupt, any call to Advance(false) is a functional
  // no-op.
  virtual void Advance(bool interrupt = false) = 0;

  // True if the transaction machine has reached a successful state.
  virtual bool Ok() const = 0;

  // From any non-terminal state, cancel this transaction.
  virtual void Cancel() = 0;

  // Block and wait for this transaction to enter a terminal state.
  virtual void Wait() = 0;
};

// The states of a Control machine.  These states map to the control transaction states described in
// MUSBMHDRC section 21.2.
enum class ControlState {
  SETUP,
  SETUP_IRQ,
  IN_DATA,
  IN_DATA_IRQ,
  OUT_DATA,
  OUT_DATA_IRQ,
  IN_STATUS,
  IN_STATUS_IRQ,
  OUT_STATUS,
  OUT_STATUS_IRQ,
  SUCCESS,
  ERROR,
  CANCEL,
};

// The individual types of a Control.  See: MUSBMHDRC section 21.2.
enum class ControlType {
  ZERO,
  READ,
  WRITE,
};

// A Control transaction is a state machine capable of processing USB control-type transfers.  The
// state progression of this machine is described by MUSBMHDRC section 21.2.
class Control : public Transaction {
 public:
  Control(ControlType type, ddk::MmioView usb, const usb_setup_t& req, void* buf, size_t len,
          size_t max_pkt_sz, uint8_t faddr)
      : type_(type),
        usb_(usb),
        req_(req),
        state_(ControlState::SETUP),
        irq_wait_(false),
        terminal_(false),
        buffer_(buf),
        len_(len),
        max_pkt_sz0_(max_pkt_sz),
        actual_(0),
        faddr_(faddr) {}

  ~Control() = default;

  // Assign, copy, and move disallowed.
  Control(Control&&) = delete;
  Control& operator=(Control&&) = delete;

  size_t actual() const override { return actual_.load(); }
  ControlState state() const { return state_.load(); }

  void Advance(bool interrupt = false) override;
  bool Ok() const override { return state_.load() == ControlState::SUCCESS; }
  void Cancel() override;
  void Wait() override { __UNUSED auto _ = sync_completion_wait(&complete_, ZX_TIME_INFINITE); }

 private:
  // Abort the machine with the given state.  Endpoint-FIFOs will be flushed.
  void AbortAs(ControlState state);

  // True if the interrupt-registers indicate a bus-error event has occurred.  See: MUSBMHDRC
  // section 21.2.1.
  bool BusError();

  // Advance from the given state to the next appropriate state.
  // clang-format off
    void AdvanceSetup()        TA_REQ(lock_);
    void AdvanceSetupIrq()     TA_REQ(lock_);
    void AdvanceInData()       TA_REQ(lock_);
    void AdvanceInDataIrq()    TA_REQ(lock_);
    void AdvanceOutData()      TA_REQ(lock_);
    void AdvanceOutDataIrq()   TA_REQ(lock_);
    void AdvanceInStatus()     TA_REQ(lock_);
    void AdvanceInStatusIrq()  TA_REQ(lock_);
    void AdvanceOutStatus()    TA_REQ(lock_);
    void AdvanceOutStatusIrq() TA_REQ(lock_);
    void AdvanceSuccess()      TA_REQ(lock_);
    void AdvanceError()        TA_REQ(lock_);
    void AdvanceCancel()       TA_REQ(lock_);
  // clang-format on

  // The type of this Control.
  const ControlType type_;

  // USB register mmio.
  ddk::MmioView usb_ TA_GUARDED(lock_);

  // The USB control request header, see USB 2.0 spec. section 9.3.
  const usb_setup_t req_;

  // The current Control machine state.
  std::atomic<ControlState> state_;

  // True if the machine is currently awaiting an asynchronous interrupt (i.e. awaiting a call to
  // Advance(true)).
  std::atomic_bool irq_wait_;

  // True if the machine is in a terminal state.
  bool terminal_ TA_GUARDED(lock_);

  // The data buffer corresponding to the transaction.  For ZERO-type transactions, this data
  // buffer is not used.  For WRITE-type transactions, this buffer will be read and its data
  // written to an endpoint-FIFO.  Similarity, for a READ-type transaction, the endpoint-FIFO
  // will be read and its data written to this buffer.  This object does not assume ownership of
  // this pointer.
  void* buffer_ TA_GUARDED(lock_);

  // The buffer size.
  const size_t len_;

  // The maximum control packet size read from the device descriptor during enumeration.
  const size_t max_pkt_sz0_;

  // The actual count of bytes transfered in either a READ or WRITE-type transaction.
  std::atomic_size_t actual_;

  // A completion which is signaled when this transaction is in a terminal state.
  sync_completion_t complete_;

  // A lock used to serialize Advance() operations which are inherently thread-hostile.
  fbl::Mutex lock_;

  // The id of the device this transaction is associated with.
  const uint8_t faddr_;
};

// The states of a BulkTransaction machine.  These states map to the operation described in
// MUSBMHDRC section 22.2.
enum class BulkState {
  SETUP,
  SETUP_IN,
  SETUP_OUT,
  SEND,
  SEND_IRQ,
  RECV,
  RECV_IRQ,
  SUCCESS,
  ERROR,
  CANCEL,
};

// The endpoint's transaction direction (always from the host's perspective).
enum class BulkDirection {
  IN,
  OUT,
};

// A BulkBase transaction is an abstract state machine whose derived types are capable of processing
// bulk-like transfers (e.g. bulk, interrupt, etc...).  Concrete subclasses need to provide an
// implementation of:
//   - AdvanceSetupIn()
//   - AdvanceSetupOut()
//
// Once configured with the necessary setup logic, the transaction will proceed in a manner
// consistent with MUSBMHDRC chapter 22.
class BulkBase : public Transaction {
 public:
  BulkBase(ddk::MmioView usb, void* buf, size_t len, const usb_endpoint_descriptor_t& desc)
      : state_(BulkState::SETUP),
        ep_(usb_ep_num(&desc)),
        max_pkt_sz_(usb_ep_max_packet(&desc)),
        usb_(usb),
        dir_(usb_ep_direction(&desc) ? BulkDirection::IN : BulkDirection::OUT),
        irq_wait_(false),
        terminal_(false),
        buffer_(buf),
        len_(len),
        actual_(0) {}

  ~BulkBase() = default;

  // Assign, copy, and move disallowed.
  BulkBase(BulkBase&&) = delete;
  BulkBase& operator=(BulkBase&&) = delete;

  size_t actual() const override { return actual_.load(); }
  BulkState state() const { return state_.load(); }

  void Advance(bool interrupt = false) override;
  bool Ok() const override { return state_.load() == BulkState::SUCCESS; }
  void Cancel() override;
  void Wait() override { __UNUSED auto _ = sync_completion_wait(&complete_, ZX_TIME_INFINITE); }

 protected:
  // The current machine state.
  std::atomic<BulkState> state_;

  // The endpoint which this transaction is associated with.
  const uint8_t ep_;

  // The maximum bulk packet size read from the endpoint descriptor.
  const size_t max_pkt_sz_;

  // USB register mmio.
  ddk::MmioView usb_ TA_GUARDED(lock_);

 private:
  // Abort the machine with the given state.  All endpoint-FIFOs will be flushed.
  void AbortAs(BulkState state);

  // True if the interrupt-registers indicate a bus-error event has occurred.
  bool BusError();

  // Advance from the given state to the next appropriate state.
  // clang-format off
    void AdvanceSetup()            TA_REQ(lock_);
    virtual void AdvanceSetupIn()  TA_REQ(lock_) = 0;
    virtual void AdvanceSetupOut() TA_REQ(lock_) = 0;
    void AdvanceSend()             TA_REQ(lock_);
    void AdvanceSendIrq()          TA_REQ(lock_);
    void AdvanceRecv()             TA_REQ(lock_);
    void AdvanceRecvIrq()          TA_REQ(lock_);
    void AdvanceSuccess()          TA_REQ(lock_);
    void AdvanceError()            TA_REQ(lock_);
    void AdvanceCancel()           TA_REQ(lock_);
  // clang-format on

  // The endpoint direction for this transaction.
  const BulkDirection dir_;

  // True if the machine is currently awaiting an asynchronous interrupt (i.e. awaiting a call to
  // Advance(true)).
  std::atomic_bool irq_wait_;

  // True if the machine is in a terminal state.
  bool terminal_ TA_GUARDED(lock_);

  // The data buffer corresponding to the transaction.  For transactions corresponding to OUT-type
  // endpoints, this data will be read and transfered to the device.  Similarily, for IN-type
  // endpoints, device data will be read and written to this buffer.
  void* buffer_ TA_GUARDED(lock_);

  // The buffer size.
  const size_t len_;

  // True if a block of transferred data was aligned to the packet size.  Note that a zero-length
  // transfer is not considered packet aligned.
  bool pkt_aligned_ TA_GUARDED(lock_);

  // The actual count of bytes transfered in either an IN or OUT-type transaction.  If more than
  // max_pkt_sz_ bytes need to be read/written, this value accumulates the total count of bytes as
  // multiple packets are processed.  The total number of bytes read/written will be available
  // when the machine reaches a terminal state.
  std::atomic_size_t actual_;

  // A completion which is signaled when this transaction is in a terminal state.
  sync_completion_t complete_;

  // A lock used to serialize Advance() operations which are inherently thread-hostile.
  fbl::Mutex lock_;
};

// A Bulk transaction is a state machine capable of processing USB Bulk-type transfers.  The state
// progression of this machine is described by MUSBMHDRC chapter 22.
class Bulk : public BulkBase {
 public:
  Bulk(ddk::MmioView usb, uint8_t faddr, void* buf, size_t len,
       const usb_endpoint_descriptor_t& desc)
      : BulkBase(usb, buf, len, desc), interval_(desc.bInterval), faddr_(faddr) {}

 private:
  // Configure MUSBMHDRC for USB Bulk-type transfer.  See: MUSBMHDRC section 22.2.
  void AdvanceSetupIn() override;
  void AdvanceSetupOut() override;

  // The bulk-transfer NAK timeout window.
  const uint8_t interval_;

  // The id of the device this transaction is associated with.
  const uint8_t faddr_;
};

// An Interrupt transaction is a state machine capable of processing USB bulk-type transfers.  The
// state progression of this machine is described by MUSBMHDRC chapter 23.
class Interrupt : public BulkBase {
 public:
  Interrupt(ddk::MmioView usb, uint8_t faddr, void* buf, size_t len,
            const usb_endpoint_descriptor_t& desc)
      : BulkBase(usb, buf, len, desc), interval_(desc.bInterval), faddr_(faddr) {}

 private:
  // Configure MUSBMHDRC for USB Interrupt-type transfer.  See: MUSBMHDRC section 23.2.
  void AdvanceSetupIn() override;
  void AdvanceSetupOut() override;

  // The data transfer endpoint polling period read from the endpoint descriptor.
  const uint8_t interval_;

  // The id of the device this transaction is associated with.
  const uint8_t faddr_;
};

}  // namespace mt_usb_hci
