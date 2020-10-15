// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/sysmem_allocator.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/fit/scope.h>
#include <lib/syslog/cpp/macros.h>

namespace {

constexpr uint32_t kNamePriority = 30;  // Higher than Scenic but below the maximum.
constexpr uint32_t kMaxAttempts = 10;
constexpr uint32_t kInitialDelayMs = 200;
constexpr uint32_t kFinalDelayMs = 1000;

// After freeing a non-shared buffer collection, we wait some time to give sysmem a chance to fully
// free that memory.
constexpr zx::duration TimeoutForAttempt(uint32_t attempt) {
  return zx::msec(kInitialDelayMs +
                  ((kFinalDelayMs - kInitialDelayMs) * attempt) / (kMaxAttempts - 1));
}

// Returns a promise that completes after calling |WaitForBuffersAllocated| on the provided
// BufferCollection.
//
// The |collection| is consumed by this operation and will be closed upon both success and failure.
fit::promise<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> WaitForBuffersAllocated(
    fuchsia::sysmem::BufferCollectionPtr collection) {
  // Move the bridge completer into a shared_ptr so that we can share the completer between the
  // FIDL error handler and the WaitForBuffersAllocated callback.
  fit::bridge<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> bridge;
  auto completer =
      std::make_shared<fit::completer<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>>(
          std::move(bridge.completer));
  std::weak_ptr<fit::completer<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>>
      weak_completer(completer);
  collection.set_error_handler([completer](zx_status_t status) {
    // After calling SetConstraints, allocation may fail. This results in WaitForBuffersAllocated
    // returning NO_MEMORY followed by channel closure. Because the client may observe these in
    // either order, treat channel closure as if it were NO_MEMORY.
    FX_CHECK(status != ZX_OK);
    completer->complete_error(status == ZX_ERR_PEER_CLOSED ? ZX_ERR_NO_MEMORY : status);
  });
  collection->WaitForBuffersAllocated(
      [weak_completer](zx_status_t status,
                       fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        auto completer = weak_completer.lock();
        if (completer) {
          if (status == ZX_OK) {
            completer->complete_ok(std::move(buffers));
          } else {
            completer->complete_error(status);
          }
        }
      });
  return bridge.consumer.promise().inspect(
      [collection = std::move(collection)](
          const fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>& result) mutable {
        if (collection) {
          collection->Close();
          collection = nullptr;
        }
      });
}

}  // namespace

fit::promise<void, zx_status_t> SysmemAllocator::ProbeForFreeSpace(
    const fuchsia::sysmem::BufferCollectionConstraints& constraints,
    std::unique_ptr<SysmemAllocator::ProbeState> state) {
  TRACE_DURATION("camera", "SysmemAllocator::ProbeForFreeSpace", "attempt", state->attempt);
  // Attempt to allocate a non-shared collection.
  fuchsia::sysmem::BufferCollectionPtr collection;
  allocator_->AllocateNonSharedCollection(collection.NewRequest());
  collection->SetName(0, "FreeSpaceProbe");
  collection->SetConstraints(true, constraints);

  // Move the bridge completer into a shared_ptr so that we can share the completer between the
  return WaitForBuffersAllocated(std::move(collection))
      // Add a delay after completion to allow the prevous allocation to be free'd by sysmem.
      .and_then([this, state = state.get()](fuchsia::sysmem::BufferCollectionInfo_2&) {
        TRACE_DURATION("camera", "SysmemAllocator::ProbeForFreeSpace.delay");
        TRACE_FLOW_STEP("camera", "SysmemAllocator.probe_for_free_space", state->nonce);
        fit::bridge<void, zx_status_t> bridge;
        async::PostDelayedTask(
            dispatcher_,
            [completer = std::move(bridge.completer)]() mutable { completer.complete_ok(); },
            TimeoutForAttempt(state->attempt));
        return bridge.consumer.promise();
      })
      // Try again if appropriate, otherwise return ok or error.
      .then([this, state = std::move(state), constraints](
                fit::result<void, zx_status_t>& result) mutable -> fit::promise<void, zx_status_t> {
        TRACE_DURATION("camera", "SysmemAllocator::ProbeForFreeSpace.complete_or_retry");
        TRACE_FLOW_STEP("camera", "SysmemAllocator.probe_for_free_space", state->nonce);
        if (++state->attempt < kMaxAttempts && result.is_error() &&
            result.error() == ZX_ERR_NO_MEMORY) {
          return ProbeForFreeSpace(constraints, std::move(state));
        }
        return fit::make_result_promise(std::move(result));
      });
}

SysmemAllocator::SysmemAllocator(async_dispatcher_t* dispatcher,
                                 fuchsia::sysmem::AllocatorHandle allocator)
    : dispatcher_(dispatcher), allocator_(allocator.Bind()) {}

fit::promise<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>
SysmemAllocator::SafelyBindSharedCollection(
    fuchsia::sysmem::BufferCollectionTokenHandle token,
    fuchsia::sysmem::BufferCollectionConstraints constraints, std::string name) {
  TRACE_DURATION("camera", "SysmemAllocator::BindSharedCollection");
  return WaitForFreeSpace(constraints)
      .then([this, token = std::move(token), constraints,
             name = std::move(name)](const fit::result<>& result) mutable
            -> fit::promise<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> {
        if (result.is_error()) {
          return fit::make_result_promise<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t>(
              fit::error(ZX_ERR_NO_MEMORY));
        }
        // We expect sysmem to have free space, so bind the provided token now.
        fuchsia::sysmem::BufferCollectionPtr collection;
        allocator_->BindSharedCollection(std::move(token), collection.NewRequest());
        collection->SetName(kNamePriority, std::move(name));
        collection->SetConstraints(true, constraints);
        return WaitForBuffersAllocated(std::move(collection));
      })
      .wrap_with(serialize_requests_)
      .wrap_with(scope_);
}

fit::promise<> SysmemAllocator::WaitForFreeSpace(
    fuchsia::sysmem::BufferCollectionConstraints constraints) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "SysmemAllocator::WaitForFreeSpace");
  TRACE_FLOW_BEGIN("camera", "SysmemAllocator.probe_for_free_space", nonce);
  // Incorporate typical client constraints.
  constexpr uint32_t kMaxClientBuffers = 5;
  constexpr uint32_t kBytesPerRowDivisor = 32 * 16;  // GPU-optimal stride.
  constraints.min_buffer_count_for_camping += kMaxClientBuffers;
  for (auto& format_constraints : constraints.image_format_constraints) {
    format_constraints.bytes_per_row_divisor =
        std::max(format_constraints.bytes_per_row_divisor, kBytesPerRowDivisor);
  }

  auto state = std::make_unique<ProbeState>();
  state->nonce = nonce;
  return ProbeForFreeSpace(constraints, std::move(state))
      .then([nonce](fit::result<void, zx_status_t>& result) -> fit::result<> {
        TRACE_DURATION("camera", "SysmemAllocator::ProbeForFreeSpace.completion");
        TRACE_FLOW_END("camera", "SysmemAllocator.probe_for_free_space", nonce);
        if (result.is_ok()) {
          return fit::ok();
        } else {
          return fit::error();
        }
      });
}
