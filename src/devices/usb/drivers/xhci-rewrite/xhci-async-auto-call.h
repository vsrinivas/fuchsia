// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_ASYNC_AUTO_CALL_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_ASYNC_AUTO_CALL_H_

#include "usb-xhci.h"

namespace usb_xhci {
// A reference counted class that automatically
// invokes a promise when its reference count reaches zero.
class AsyncAutoCall : public fbl::RefCounted<AsyncAutoCall> {
 public:
  // Constructs an AsyncAutoCall bound to a UsbXhci instance.
  // It is the caller's responsibility to ensure that this
  // does not outlive UsbXhci. In order to do this, the caller
  // should ensure the following invariants:
  // * The caller cannot explicitly transfer an AsyncAutoCall
  // between threads unless done through PostCallback on the UsbXhci
  // instance.
  // * The caller should ensure that an AsyncAutoCall does not outlive
  // the scope of its associated promises.
  // * All promises associated with this AsyncAutoCall should be bound
  // to UsbXhci's dispatcher.
  // UsbXhci will destroy its associated promises before being deleted,
  // ensuring that any corresponding AsyncAutoCalls are freed prior to
  // the UsbXhci pointer becoming invalid.
  explicit AsyncAutoCall(UsbXhci* hci) : hci_(hci) { Init(); }
  // Borrows the promise. The caller is expected to give it back by calling
  // GivebackPromise after it is done manipulating the promise.
  fit::promise<void, void> BorrowPromise() { return promise_.box(); }
  void GivebackPromise(fit::promise<void, void> promise) { promise_ = promise.box(); }

  // Reinitializes a cancelled async auto call
  void Reinit() { Init(); }
  void Cancel() { completer_.reset(); }
  ~AsyncAutoCall() {
    if (completer_.has_value()) {
      completer_->complete_ok();
      hci_->ScheduleTask(
          promise_
              .then([=](fit::result<void, void>& result) -> fit::result<TRB*, zx_status_t> {
                return fit::ok<TRB*>(nullptr);
              })
              .box());
    }
  }

 private:
  void Init() {
    fit::bridge<void, void> bridge;
    promise_ = bridge.consumer.promise()
                   .then([=](fit::result<void, void>& result) { return result; })
                   .box();
    completer_ = std::move(bridge.completer);
  }
  fit::promise<void, void> promise_;
  std::optional<fit::completer<void, void>> completer_;
  UsbXhci* hci_;
};
}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_ASYNC_AUTO_CALL_H_
