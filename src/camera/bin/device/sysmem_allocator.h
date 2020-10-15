// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_SYSMEM_ALLOCATOR_H_
#define SRC_CAMERA_BIN_DEVICE_SYSMEM_ALLOCATOR_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fit/promise.h>
#include <lib/fit/scope.h>
#include <lib/fit/sequencer.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>

class SysmemAllocator {
 public:
  SysmemAllocator(async_dispatcher_t* dispatcher, fuchsia::sysmem::AllocatorHandle allocator);

  const fuchsia::sysmem::AllocatorPtr& fidl() const { return allocator_; }

  // This method will first make some secondary sysmem allocations using the same `constraints` in
  // order to probe if sysmem has sufficient memory to bind `token`. Once it is determined that
  // sysmem has sufficent memory, `token` will be bound to a new `BufferCollection` and `name` and
  // `constraints` will be set on the collection. Upon a successful allocation, the collection will
  // be closed and the `BufferCollectionInfo_2` will be returned by the promise.
  //
  // An error is returned from the promise if the allocation is unable to complete for any reason.
  fit::promise<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> SafelyBindSharedCollection(
      fuchsia::sysmem::BufferCollectionTokenHandle token,
      fuchsia::sysmem::BufferCollectionConstraints constraints, std::string name);

 private:
  // Returns a promise that completes when we expect sysmem to have sufficient free space to be
  // able to allocate a buffer with `constraints`.
  //
  // The promise will complete with an error if there was a timeout waiting for sysmem to
  // successfully allocate.
  fit::promise<> WaitForFreeSpace(fuchsia::sysmem::BufferCollectionConstraints constraints);

  struct ProbeState {
    uint32_t attempt = 0;
    trace_async_id_t nonce;
  };
  fit::promise<void, zx_status_t> ProbeForFreeSpace(
      const fuchsia::sysmem::BufferCollectionConstraints& constraints,
      std::unique_ptr<ProbeState> attempts);

  async_dispatcher_t* dispatcher_;
  fuchsia::sysmem::AllocatorPtr allocator_;

  // Use a sequencer to enforce that only a single request is in flight at a time. This is needed
  // because the 'free space probe' operation allocates some intermediate buffers that could exhaust
  // sysmem memory if multiple requests are processed concurrently.
  fit::sequencer serialize_requests_;
  fit::scope scope_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_SYSMEM_ALLOCATOR_H_
