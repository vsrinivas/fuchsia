// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
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
};

}  // namespace usb_xhci
