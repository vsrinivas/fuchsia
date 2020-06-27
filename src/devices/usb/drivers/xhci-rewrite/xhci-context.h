// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_CONTEXT_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_CONTEXT_H_
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/slab_allocator.h>
#include <usb/request-cpp.h>

#include "registers.h"
#include "synchronous_executor.h"
#include "xhci-hub.h"

namespace usb_xhci {
struct TRBContext;

using Request = usb::BorrowedRequest<void>;
using OwnedRequest = usb::Request<void>;

using TRBPromise = fit::promise<TRB*, zx_status_t>;
using AllocatorTraits = fbl::InstancedSlabAllocatorTraits<std::unique_ptr<TRBContext>, 4096U>;
using AllocatorType = fbl::SlabAllocator<AllocatorTraits>;
struct TRBContext : fbl::DoublyLinkedListable<std::unique_ptr<TRBContext>>,
                    fbl::SlabAllocated<AllocatorTraits> {
  // Root hub port number
  uint8_t port_number = 0;
  std::optional<HubInfo> hub_info;
  std::optional<Request> request;
  std::optional<fit::completer<TRB*, zx_status_t>> completer;
  uint64_t token;
  TRB* trb = nullptr;
  TRB* first_trb = nullptr;
  size_t short_length = 0;
  size_t transfer_len_including_short_trb = 0;
};

}  // namespace usb_xhci

// Specializations of some fit methods to make code more ergnomic.
namespace fit {
inline promise_impl<::fit::internal::result_continuation<usb_xhci::TRB*, zx_status_t>>
make_error_promise(zx_status_t error) {
  return make_result_promise<usb_xhci::TRB*, zx_status_t>(fit::error(error));
}

inline promise_impl<::fit::internal::result_continuation<usb_xhci::TRB*, zx_status_t>>
make_ok_promise(usb_xhci::TRB* trb) {
  return make_result_promise<usb_xhci::TRB*, zx_status_t>(fit::ok(trb));
}
}  // namespace fit

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_XHCI_CONTEXT_H_
