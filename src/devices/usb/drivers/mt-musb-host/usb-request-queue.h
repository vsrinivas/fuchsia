// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_REQUEST_QUEUE_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_REQUEST_QUEUE_H_

#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <threads.h>
#include <zircon/hw/usb.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>

#include "usb-transaction.h"

namespace mt_usb_hci {

// The maximum single-buffered endpoint FIFO size.
constexpr uint32_t kFifoMaxSize = 4096;

// An RequestQueue cultivates a queue of outstanding usb requests and asynchronously services them
// in serial-FIFO order.
class RequestQueue {
 public:
  virtual ~RequestQueue() = default;

  // Advance processing of the current request which may optionally be the result of servicing a
  // hardware IRQ event (in which case interrupt should be set to true).
  virtual void Advance(bool interrupt) = 0;

  // Enqueue a new request for processing.
  virtual zx_status_t QueueRequest(usb::BorrowedRequest<> req) = 0;

  // Start the request processing thread.
  virtual zx_status_t StartQueueThread() = 0;

  // Clear and cancel all currently pending requests from the queue.
  virtual zx_status_t CancelAll() = 0;

  // Return this endpoint's maximum packet transfer size (i.e. w_max_packet_size).
  virtual size_t GetMaxTransferSize() = 0;

  // Halt endpoint request processing.  All outstanding requests will result in a
  // ZX_ERR_IO_NOT_PRESENT status, and the queue thread will be shut down.
  virtual zx_status_t Halt() = 0;
};

// A TransactionQueue is an RequestQueue which dispatches requests to a Transaction for
// processing.
class TransactionQueue : public RequestQueue {
 public:
  TransactionQueue(ddk::MmioView usb, uint8_t faddr, const usb_endpoint_descriptor_t& descriptor)
      : usb_(usb),
        faddr_(faddr),
        max_pkt_sz_(usb_ep_max_packet(&descriptor)),
        descriptor_(descriptor),
        halted_(false) {}

  ~TransactionQueue() = default;

  void Advance(bool interrupt) override { transaction_->Advance(interrupt); }
  zx_status_t QueueRequest(usb::BorrowedRequest<> req) override;
  zx_status_t StartQueueThread() override;
  zx_status_t CancelAll() override;
  size_t GetMaxTransferSize() override;
  zx_status_t Halt() override;

 protected:
  // The USB register mmio.
  ddk::MmioView usb_;

  // A transaction type used by this endpoint.
  std::unique_ptr<Transaction> transaction_;

  // The id of the device this endpoint is associated with.
  uint8_t faddr_;

  // The maximum usb packet size for this transaction.
  size_t max_pkt_sz_;

  // The enumerated endpoint descriptor describing the behavior of this endpoint.
  usb_endpoint_descriptor_t descriptor_;

  // True if this endpoint has been halted.
  std::atomic_bool halted_;

 private:
  // Dispatch and process a request transaction.  This method blocks until the transaction is
  // complete.
  virtual zx_status_t DispatchRequest(usb::BorrowedRequest<> req) = 0;

  // Queue thread which services enqueued requests in serial FIFO order.
  int QueueThread();

  // The queue of pending usb::BorrowedRequests ready to be dispatched.  Requests are dispatched
  // and processed in FIFO-order.
  usb::BorrowedRequestQueue<> pending_ TA_GUARDED(pending_lock_);

  // Queue dispatch thread.
  thrd_t pending_thread_;

  // Queue dispatch condition and associated mutex.
  fbl::Mutex pending_lock_;
  fbl::ConditionVariable pending_cond_ TA_GUARDED(pending_lock_);
};

// A ControlQueue is a TransactionQueue dispatching control-type transactions.
class ControlQueue : public TransactionQueue {
 public:
  // Note that initially all enumeration control transactions are performed on the default
  // control-pipe address of 0 using the spec. default maximum packet size of 8 bytes (encoded in
  // this type's static descriptor).  During enumeration, these values will be updated to their
  // final configured values.
  explicit ControlQueue(ddk::MmioView usb) : TransactionQueue(usb, 0, descriptor_) {}

  // Read the device descriptor (used only for enumeration).  Note that a successful
  // GET_DESCRIPTOR transaction will result in max_pkt_sz_ being updated with the bMaxPacketSize0
  // as returned by the device.
  zx_status_t GetDeviceDescriptor(usb_device_descriptor_t* out);

  // Set the USB function address for the device this endpoint is associated with (used only for
  // enumeration).  Note that a successful SET_ADDRESS transaction will result in faddr_  being
  // updated with the configured address.
  zx_status_t SetAddress(uint8_t addr);

 private:
  zx_status_t DispatchRequest(usb::BorrowedRequest<> req) override;

  // An endpoint descriptor containing sufficient data to bootstrap a Control transaction.
  static constexpr usb_endpoint_descriptor_t descriptor_ = {
      0,             // .b_length
      0,             // .b_descriptor_type
      0,             // .b_endpoint_address
      0,             // .bm_attributes
      htole16(0x8),  // .w_max_packet_size (the only piece of data consumed)
      0,             // .b_interval
  };
};

// A BulkQueue is a TransactionQueue dispatching bulk-type transactions.
class BulkQueue : public TransactionQueue {
 public:
  BulkQueue(ddk::MmioView usb, uint8_t faddr, const usb_endpoint_descriptor_t& descriptor)
      : TransactionQueue(usb, faddr, descriptor) {}

 private:
  zx_status_t DispatchRequest(usb::BorrowedRequest<> req) override;
};

// An InterruptQueue is a TransactionQueue dispatching interrupt-type transactions.
class InterruptQueue : public TransactionQueue {
 public:
  InterruptQueue(ddk::MmioView usb, uint8_t faddr, const usb_endpoint_descriptor_t& descriptor)
      : TransactionQueue(usb, faddr, descriptor) {}

 private:
  zx_status_t DispatchRequest(usb::BorrowedRequest<> req) override;
};

}  // namespace mt_usb_hci

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_USB_REQUEST_QUEUE_H_
