// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysmem-secure-mem-server.h"

#include <lib/async-loop/default.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>

#include <cinttypes>
#include <limits>

#include <fbl/algorithm.h>
#include <safemath/safe_math.h>

#include "fidl/fuchsia.sysmem/cpp/wire_types.h"
#include "lib/async/cpp/task.h"
#include "lib/fidl/llcpp/arena.h"
#include "lib/fidl/llcpp/vector_view.h"
#include "log.h"

namespace {

constexpr uint32_t kProtectedRangeGranularity = 64u * 1024;
constexpr uint32_t kMaxClientUsableProtectedRangeCount = 11;

bool VerifyRange(uint64_t physical_address, size_t size_bytes, uint32_t required_alignment) {
  if (physical_address % required_alignment != 0) {
    LOG(ERROR, "physical_address not divisible by required_alignment");
    return false;
  }

  if (size_bytes % required_alignment != 0) {
    LOG(ERROR, "size_bytes not divisible by required_alignment");
    return false;
  }

  if (size_bytes == 0) {
    LOG(ERROR, "heap.size_bytes == 0");
    return false;
  }

  if (!safemath::IsValueInRangeForNumericType<uint32_t>(physical_address)) {
    LOG(ERROR, "heap.physical_address > 0xFFFFFFFF");
    return false;
  }

  if (!safemath::IsValueInRangeForNumericType<uint32_t>(size_bytes)) {
    LOG(ERROR, "heap.size_bytes > 0xFFFFFFFF");
    return false;
  }

  if (!safemath::CheckAdd(physical_address, size_bytes).IsValid<uint32_t>()) {
    // At least For now, we are rejecting any range whose last page is the page
    // that contains 0xFFFFFFFF.   It's probably best to continue rejecting any
    // such range at least until we've covered that case in focused testing.
    // If we wanted to allow such a range we'd subtract 1 from size_bytes for
    // the argument passed to CheckAdd() just above.
    LOG(ERROR, "start + size overflow");
    return false;
  }

  return true;
}

bool ValidateSecureHeapRange(const fuchsia_sysmem::wire::SecureHeapRange& range) {
  if (!range.has_physical_address()) {
    LOG(INFO, "!range.has_physical_address()");
    return false;
  }

  if (!range.has_size_bytes()) {
    LOG(INFO, "!range.has_size_bytes()");
    return false;
  }

  if (range.physical_address() > (1ull << 32) - kProtectedRangeGranularity) {
    LOG(INFO, "physical_address() > (1ull << 32) - kProtectedRangeGranularity");
    return false;
  }

  if (range.size_bytes() > std::numeric_limits<uint32_t>::max()) {
    LOG(INFO, "size_bytes() > 0xFFFFFFFF");
    return false;
  }

  if (range.physical_address() + range.size_bytes() > (1ull << 32)) {
    LOG(INFO, "physical_address() + size_bytes() > (1ull << 32)");
    return false;
  }

  return true;
}

bool ValidateSecureHeapAndRange(const fuchsia_sysmem::wire::SecureHeapAndRange& heap_range,
                                bool is_zeroing) {
  if (!heap_range.has_heap()) {
    LOG(INFO, "!heap_range.has_heap()");
    return false;
  }

  if (is_zeroing) {
    if (heap_range.heap() != fuchsia_sysmem::wire::HeapType::kAmlogicSecure &&
        heap_range.heap() != fuchsia_sysmem::wire::HeapType::kAmlogicSecureVdec) {
      LOG(INFO, "heap_range.heap() != kAmLogicSecure && heap_range.heap() != kAmlogicSecureVdec");
      return false;
    }
  } else {
    if (heap_range.heap() != fuchsia_sysmem::wire::HeapType::kAmlogicSecure) {
      LOG(INFO, "heap_range.heap() != kAmlogicSecure");
      return false;
    }
  }

  if (!heap_range.has_range()) {
    LOG(INFO, "!heap_range.has_range()");
    return false;
  }

  if (!ValidateSecureHeapRange(heap_range.range())) {
    return false;
  }

  return true;
}

bool ValidateSecureHeapAndRangeModification(
    const fuchsia_sysmem::wire::SecureHeapAndRangeModification& range_modification) {
  if (!range_modification.has_heap()) {
    LOG(INFO, "!range_modification.has_heap()");
    return false;
  }

  if (range_modification.heap() != fuchsia_sysmem::wire::HeapType::kAmlogicSecure) {
    LOG(INFO, "heap_range.heap() != kAmlogicSecure");
    return false;
  }

  if (!range_modification.has_old_range()) {
    LOG(INFO, "!range_modification.has_old_range()");
    return false;
  }

  if (!range_modification.has_new_range()) {
    LOG(INFO, "!range_modification.has_new_range()");
    return false;
  }

  const auto& old_range = range_modification.old_range();
  const auto& new_range = range_modification.new_range();

  if (!ValidateSecureHeapRange(old_range)) {
    LOG(INFO, "!ValidateSecureHeapRange(old_range)");
    return false;
  }

  if (!ValidateSecureHeapRange(new_range)) {
    LOG(INFO, "!ValidateSecureHeapRange(new_range)");
    return false;
  }

  if (new_range.physical_address() != old_range.physical_address() &&
      new_range.physical_address() + new_range.size_bytes() !=
          old_range.physical_address() + old_range.size_bytes()) {
    LOG(INFO, "old_range and new_range do not match in start or end");
    return false;
  }

  if (old_range.physical_address() == new_range.physical_address() &&
      old_range.size_bytes() == new_range.size_bytes()) {
    LOG(INFO, "old_range and new_range are the same");
    return false;
  }

  if (old_range.size_bytes() == 0) {
    LOG(INFO, "old_range is empty");
    return false;
  }

  // new range is allowed to be empty, which effectively becomes a delete

  return true;
}

}  // namespace

SysmemSecureMemServer::SysmemSecureMemServer(const fdf_dispatcher_t* fdf_dispatcher,
                                             zx::channel tee_client_channel)
    : fdf_dispatcher_(fdf_dispatcher), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(tee_client_channel);
  tee_connection_.Bind(std::move(tee_client_channel));
}

SysmemSecureMemServer::~SysmemSecureMemServer() {
  ZX_DEBUG_ASSERT(fdf_dispatcher_get_current_dispatcher() == fdf_dispatcher_);
  ZX_DEBUG_ASSERT(ranges_.empty());
  ZX_DEBUG_ASSERT(is_loop_done_ || !was_thread_started_);
  if (was_thread_started_) {
    // Call StopAsync() first, and only do ~SysmemSecureMemServer() when secure_mem_server_done has
    // been called.
    ZX_DEBUG_ASSERT(loop_.GetState() == ASYNC_LOOP_QUIT);
    ZX_DEBUG_ASSERT(is_loop_done_);
    // EnsureLoopDone() was already previously called.
    ZX_DEBUG_ASSERT(!secure_mem_server_done_);
    loop_.JoinThreads();
    // This will cancel the wait, which will run EnsureLoopDone(), but since is_loop_done_ is
    // already true, the EnsureLoopDone() that runs here won't do anything.  This call to Shutdown()
    // is necessary to complete the llcpp unbind.
    loop_.Shutdown();
  }
}

zx_status_t SysmemSecureMemServer::BindAsync(zx::channel sysmem_secure_mem_server,
                                             thrd_t* sysmem_secure_mem_server_thread,
                                             SecureMemServerDone secure_mem_server_done) {
  ZX_DEBUG_ASSERT(sysmem_secure_mem_server);
  ZX_DEBUG_ASSERT(sysmem_secure_mem_server_thread);
  ZX_DEBUG_ASSERT(secure_mem_server_done);
  ZX_DEBUG_ASSERT(fdf_dispatcher_get_current_dispatcher() == fdf_dispatcher_);
  zx_status_t status = loop_.StartThread("sysmem_secure_mem_server_loop", &loop_thread_);
  if (status != ZX_OK) {
    LOG(ERROR, "loop_.StartThread() failed - status: %d", status);
    return status;
  }
  was_thread_started_ = true;
  // This probably goes without saying, but it's worth pointing out that the loop_thread_ must be
  // separate from the ddk_dispatcher_thread_ so that TEEC_* calls made on the loop_thread_ can be
  // served by the fdf dispatcher without deadlock.
  ZX_DEBUG_ASSERT(thrd_current() != loop_thread_);
  closure_queue_.SetDispatcher(loop_.dispatcher(), loop_thread_);
  *sysmem_secure_mem_server_thread = loop_thread_;
  secure_mem_server_done_ = std::move(secure_mem_server_done);
  PostToLoop([this, sysmem_secure_mem_server = std::move(sysmem_secure_mem_server)]() mutable {
    ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
    zx_status_t status = fidl::BindSingleInFlightOnly<SysmemSecureMemServer>(
        loop_.dispatcher(), std::move(sysmem_secure_mem_server), this,
        [this](SysmemSecureMemServer* sysmem_secure_mem_server) {
          // This can get called from the fdf dispatcher if
          // we're doing loop_.Shutdown() to unbind an llcpp server (so
          // far the only way to unbind an llcpp server).  However, in
          // this case the call to EnsureLoopDone() will idempotently do
          // nothing because is_loop_done_ is already true.
          ZX_DEBUG_ASSERT(thrd_current() == loop_thread_ || is_loop_done_);
          // If secure_mem_server_done_ still set by this point, !is_success.
          EnsureLoopDone(false);
        });
    if (status != ZX_OK) {
      LOG(ERROR, "fidl::BindSingleInFlightOnly() failed - status: %d", status);
      ZX_DEBUG_ASSERT(secure_mem_server_done_);
      EnsureLoopDone(false);
    }
  });
  return ZX_OK;
}

void SysmemSecureMemServer::StopAsync() {
  // The only way to unbind an llcpp server is to Shutdown() the loop, but before we can do that we
  // have to Quit() the loop.
  ZX_DEBUG_ASSERT(fdf_dispatcher_get_current_dispatcher() == fdf_dispatcher_);
  ZX_DEBUG_ASSERT(was_thread_started_);
  PostToLoop([this] {
    ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
    // Stopping the loop intentionally is considered is_success, if that happens before channel
    // failure.  EnsureLoopDone() is idempotent so it'll early out if already called previously.
    EnsureLoopDone(true);
  });
}

void SysmemSecureMemServer::GetPhysicalSecureHeaps(
    GetPhysicalSecureHeapsRequestView request, GetPhysicalSecureHeapsCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  fidl::Arena allocator;
  fuchsia_sysmem::wire::SecureHeapsAndRanges heaps;
  zx_status_t status = GetPhysicalSecureHeapsInternal(&allocator, &heaps);
  if (status != ZX_OK) {
    LOG(ERROR, "GetPhysicalSecureHeapsInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(std::move(heaps));
}

void SysmemSecureMemServer::GetPhysicalSecureHeapProperties(
    GetPhysicalSecureHeapPropertiesRequestView request,
    GetPhysicalSecureHeapPropertiesCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // must out-live Reply()
  fidl::Arena allocator;
  fuchsia_sysmem::wire::SecureHeapProperties properties;
  zx_status_t status =
      GetPhysicalSecureHeapPropertiesInternal(request->heap, allocator, &properties);
  if (status != ZX_OK) {
    LOG(INFO, "GetPhysicalSecureHeapPropertiesInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(std::move(properties));
}

void SysmemSecureMemServer::AddSecureHeapPhysicalRange(
    AddSecureHeapPhysicalRangeRequestView request,
    AddSecureHeapPhysicalRangeCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // must out-live Reply()
  fidl::Arena allocator;
  zx_status_t status = AddSecureHeapPhysicalRangeInternal(request->heap_range);
  if (status != ZX_OK) {
    LOG(INFO, "AddSecureHeapPhysicalRangeInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SysmemSecureMemServer::DeleteSecureHeapPhysicalRange(
    DeleteSecureHeapPhysicalRangeRequestView request,
    DeleteSecureHeapPhysicalRangeCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // must out-live Reply()
  fidl::Arena allocator;
  zx_status_t status = DeleteSecureHeapPhysicalRangeInternal(request->heap_range);
  if (status != ZX_OK) {
    LOG(INFO, "DeleteSecureHeapPhysicalRangesInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SysmemSecureMemServer::ModifySecureHeapPhysicalRange(
    ModifySecureHeapPhysicalRangeRequestView request,
    ModifySecureHeapPhysicalRangeCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // must out-live Reply()
  fidl::Arena allocator;
  zx_status_t status = ModifySecureHeapPhysicalRangeInternal(request->range_modification);
  if (status != ZX_OK) {
    LOG(INFO, "ModifySecureHeapPhysicalRangesInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SysmemSecureMemServer::ZeroSubRange(ZeroSubRangeRequestView request,
                                         ZeroSubRangeCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // must out-live Reply()
  fidl::Arena allocator;
  zx_status_t status =
      ZeroSubRangeInternal(request->is_covering_range_explicit, request->heap_range);
  if (status != ZX_OK) {
    LOG(INFO, "ZeroSubRangeInternal() failed - status: %d", status);
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void SysmemSecureMemServer::PostToLoop(fit::closure to_run) {
  // For now this is only expected to be called from ddk_dispatcher_thread_.
  ZX_DEBUG_ASSERT(fdf_dispatcher_get_current_dispatcher() == fdf_dispatcher_);
  closure_queue_.Enqueue(std::move(to_run));
}

bool SysmemSecureMemServer::TrySetupSecmemSession() {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  // We only try this once; if it doesn't work the first time, it's very
  // unlikely to work on retry anyway, and this avoids some retry complexity.
  if (!has_attempted_secmem_session_connection_) {
    ZX_DEBUG_ASSERT(tee_connection_.is_bound());
    ZX_DEBUG_ASSERT(!secmem_session_.has_value());

    has_attempted_secmem_session_connection_ = true;

    auto session_result = SecmemSession::TryOpen(std::move(tee_connection_));
    if (!session_result.is_ok()) {
      // Logging handled in `SecmemSession::TryOpen`
      tee_connection_ = session_result.take_error();
      return false;
    }

    secmem_session_.emplace(session_result.take_value());
    LOG(DEBUG, "Successfully connected to secmem session");
    return true;
  }

  return secmem_session_.has_value();
}

void SysmemSecureMemServer::EnsureLoopDone(bool is_success) {
  if (is_loop_done_) {
    return;
  }
  // We can't assert this any sooner, because when unbinding llcpp server using loop_.Shutdown()
  // we'll be on ddk_dispatcher_thread_.  But in that case, the first run of EnsureLoopDone()
  // happened on the loop_thread_.
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  is_loop_done_ = true;
  closure_queue_.StopAndClear();
  loop_.Quit();
  if (has_attempted_secmem_session_connection_ && secmem_session_.has_value()) {
    ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
    for (auto& range : ranges_) {
      TEEC_Result tee_status = secmem_session_->ProtectMemoryRange(
          static_cast<uint32_t>(range.begin()), static_cast<uint32_t>(range.length()), false);
      if (tee_status != TEEC_SUCCESS) {
        LOG(ERROR, "SecmemSession::ProtectMemoryRange(false) failed - TEEC_Result %d", tee_status);
        ZX_PANIC("SecmemSession::ProtectMemoryRange(false) failed - TEEC_Result %d", tee_status);
      }
    }
    ranges_.clear();
    secmem_session_.reset();
  } else {
    // We could be running on the loop_thread_ or the ddk_dispatcher_thread_ in this case.
    ZX_DEBUG_ASSERT(!secmem_session_);
  }
  if (secure_mem_server_done_) {
    secure_mem_server_done_(is_success);
  }
}

zx_status_t SysmemSecureMemServer::GetPhysicalSecureHeapsInternal(
    fidl::AnyArena* allocator, fuchsia_sysmem::wire::SecureHeapsAndRanges* heaps_and_ranges) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);

  if (is_get_physical_secure_heaps_called_) {
    LOG(ERROR, "GetPhysicalSecureHeaps may only be called at most once - reply status: %d",
        ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }
  is_get_physical_secure_heaps_called_ = true;

  if (!TrySetupSecmemSession()) {
    // Logging handled in `TrySetupSecmemSession`
    return ZX_ERR_INTERNAL;
  }

  uint64_t vdec_phys_base;
  size_t vdec_size;
  zx_status_t status = SetupVdec(&vdec_phys_base, &vdec_size);
  if (status != ZX_OK) {
    LOG(ERROR, "SetupVdec failed - status: %d", status);
    return status;
  }

  auto builder = fuchsia_sysmem::wire::SecureHeapsAndRanges::Builder(*allocator);
  auto heaps = fidl::VectorView<fuchsia_sysmem::wire::SecureHeapAndRanges>(*allocator, 1);
  auto heap_builder = fuchsia_sysmem::wire::SecureHeapAndRanges::Builder(*allocator);
  heap_builder.heap(fuchsia_sysmem::wire::HeapType::kAmlogicSecureVdec);
  auto ranges = fidl::VectorView<fuchsia_sysmem::wire::SecureHeapRange>(*allocator, 1);
  auto range_builder = fuchsia_sysmem::wire::SecureHeapRange::Builder(*allocator);
  range_builder.physical_address(vdec_phys_base);
  range_builder.size_bytes(vdec_size);

  ranges[0] = range_builder.Build();
  heap_builder.ranges(ranges);
  heaps[0] = heap_builder.Build();
  builder.heaps(heaps);

  *heaps_and_ranges = builder.Build();

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::GetPhysicalSecureHeapPropertiesInternal(
    fuchsia_sysmem::wire::HeapType heap, fidl::AnyArena& allocator,
    fuchsia_sysmem::wire::SecureHeapProperties* properties) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);

  if (!TrySetupSecmemSession()) {
    // Logging in TrySetupSecmemSession
    return ZX_ERR_INTERNAL;
  }

  if (heap != fuchsia_sysmem::wire::HeapType::kAmlogicSecure) {
    LOG(INFO, "heap != kAmlogicSecure");
    return ZX_ERR_INVALID_ARGS;
  }

  is_dynamic_ = secmem_session_->DetectIsAdjustAndSkipDeviceSecureModeUpdateAvailable();
  max_range_count_ = is_dynamic_ ? kMaxClientUsableProtectedRangeCount : 1;
  is_dynamic_checked_ = true;

  auto builder = fuchsia_sysmem::wire::SecureHeapProperties::Builder(allocator);
  builder.heap(fuchsia_sysmem::wire::HeapType::kAmlogicSecure);
  builder.dynamic_protection_ranges(is_dynamic_);
  builder.protected_range_granularity(kProtectedRangeGranularity);
  builder.max_protected_range_count(max_range_count_);
  builder.is_mod_protected_range_available(is_dynamic_);

  *properties = builder.Build();

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::AddSecureHeapPhysicalRangeInternal(
    fuchsia_sysmem::wire::SecureHeapAndRange heap_range) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(ranges_.size() <= max_range_count_);

  if (!TrySetupSecmemSession()) {
    // Logging in TrySetupSecmemSession
    return ZX_ERR_INTERNAL;
  }

  if (!is_dynamic_checked_) {
    LOG(INFO, "!is_dynamic_checked_");
    return ZX_ERR_BAD_STATE;
  }

  if (!ValidateSecureHeapAndRange(heap_range, false)) {
    // Logging in ValidateSecureHeapAndRange
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(ranges_.size() <= max_range_count_);
  if (ranges_.size() == max_range_count_) {
    LOG(INFO, "range_count_ == max_range_count_");
    return ZX_ERR_BAD_STATE;
  }

  const auto range = heap_range.range();
  zx_status_t status = ProtectMemoryRange(range.physical_address(), range.size_bytes(), true);
  if (status != ZX_OK) {
    LOG(ERROR, "ProtectMemoryRange(true) failed - status: %d", status);
    return status;
  }

  const Range new_range = Range::BeginLength(range.physical_address(), range.size_bytes());
  ranges_.insert(new_range);

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::DeleteSecureHeapPhysicalRangeInternal(
    fuchsia_sysmem::wire::SecureHeapAndRange heap_range) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(ranges_.size() <= max_range_count_);

  if (!TrySetupSecmemSession()) {
    // Logging in TrySetupSecmemSession
    return ZX_ERR_INTERNAL;
  }

  if (!is_dynamic_checked_) {
    LOG(INFO, "!is_dynamic_checked_");
    return ZX_ERR_BAD_STATE;
  }

  if (!is_dynamic_) {
    LOG(ERROR,
        "DeleteSecureHeapPhysicalRangesInternal() can't be called when !dynamic - reply "
        "status: %d",
        ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  if (!ValidateSecureHeapAndRange(heap_range, false)) {
    // Logging in ValidateSecureHeapAndRange
    return ZX_ERR_INVALID_ARGS;
  }

  const Range to_delete =
      Range::BeginLength(heap_range.range().physical_address(), heap_range.range().size_bytes());
  auto to_delete_iter = ranges_.find(to_delete);
  if (to_delete_iter == ranges_.end()) {
    LOG(INFO, "ranges_.find(to_delete) == ranges_.end()");
    return ZX_ERR_NOT_FOUND;
  }

  // We could optimize this to only search a portion of ranges_, but the size limit is quite small
  // so this will be fine.
  //
  // This checks if to_delete is fully covered by other ranges.  If fully covered, we don't need to
  // zero incrementally.  If not fully covered, we do need to zero incrementally.
  Range uncovered = to_delete;
  for (auto iter = ranges_.begin(); iter != ranges_.end(); ++iter) {
    if (iter == to_delete_iter) {
      continue;
    }
    auto& range = *iter;
    if (range.end() <= uncovered.begin()) {
      continue;
    }
    if (range.begin() >= uncovered.end()) {
      break;
    }
    auto [left_remaining, right_remaining] = SubtractRanges(uncovered, range);
    if (!left_remaining.empty()) {
      // This range didn't fully cover uncovered, and no later range will either, since later ranges
      // will have begin >= range.begin, so we know uncovered isn't empty overall.
      break;
    }
    ZX_DEBUG_ASSERT(left_remaining.empty());
    if (!right_remaining.empty()) {
      // Later ranges might cover the rest.
      uncovered = right_remaining;
      continue;
    }
    ZX_DEBUG_ASSERT(right_remaining.empty());
    uncovered = right_remaining;
    break;
  }

  if (!uncovered.empty()) {
    // Shorten the range to nothing incrementally so that page zeroing doesn't happen all in one
    // call to the TEE.
    const auto range = heap_range.range();
    zx_status_t status = AdjustMemoryRange(range.physical_address(), range.size_bytes(),
                                           static_cast<uint32_t>(range.size_bytes()), false, false);
    if (status != ZX_OK) {
      LOG(INFO, "AdjustMemoryRange() failed - status: %d", status);
      return status;
    }
  } else {
    // No need for incremental zeroing since no zeroing will occur.  We can just make one call to
    // the TEE.
    const auto range = heap_range.range();
    zx_status_t status = ProtectMemoryRange(range.physical_address(), range.size_bytes(), false);
    if (status != ZX_OK) {
      LOG(ERROR, "ProtectMemoryRange(false) failed - status: %d", status);
      return status;
    }
  }

  ranges_.erase(to_delete);

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::ZeroSubRangeInternal(
    bool is_covering_range_explicit, fuchsia_sysmem::wire::SecureHeapAndRange heap_range) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(ranges_.size() <= max_range_count_);

  if (!TrySetupSecmemSession()) {
    // Logging in TrySetupSecmemSession
    return ZX_ERR_INTERNAL;
  }

  if (!is_dynamic_checked_) {
    LOG(INFO, "!is_dynamic_checked_");
    return ZX_ERR_BAD_STATE;
  }

  if (!is_dynamic_) {
    LOG(ERROR,
        "ZeroSubRangeInternal() can't be called when !dynamic - reply "
        "status: %d",
        ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  if (!ValidateSecureHeapAndRange(heap_range, true)) {
    // Logging in ValidateSecureHeapAndRange
    return ZX_ERR_INVALID_ARGS;
  }

  const Range to_zero =
      Range::BeginLength(heap_range.range().physical_address(), heap_range.range().size_bytes());

  if (is_covering_range_explicit) {
    // We don't strictly need to check for a covering range here since the TEE will do an equivalent
    // check, but doing this check here is helpful for debugging reasons.  Also, it's nice to avoid
    // situations where the first fiew chunks could zero successfully and then we could fail if the
    // next chunk isn't covered by any range.  This could occur because we zero incrementally to
    // avoid zeroing too much per call to the TEE.
    auto covering = ranges_.upper_bound(to_zero);
    if (covering != ranges_.begin()) {
      --covering;
    }
    if (covering == ranges_.end() || covering->begin() > to_zero.begin() ||
        covering->end() < to_zero.end()) {
      LOG(ERROR, "to_zero not entirely covered by a single range in ranges_");
      return ZX_ERR_NOT_FOUND;
    }

    // Similar for validating that there's no other range that overlaps - checking here is useful
    // for debugging and it's nice to avoid partial success then failure.
    bool found_another_overlapping = false;
    for (auto iter = ranges_.begin(); iter != ranges_.end(); ++iter) {
      if (iter == covering) {
        continue;
      }
      if (iter->end() <= to_zero.begin()) {
        continue;
      }
      if (iter->begin() >= to_zero.end()) {
        continue;
      }
      found_another_overlapping = true;
      break;
    }
    if (found_another_overlapping) {
      LOG(ERROR, "ZeroSubRangeInternal() found a second range that overlaps; this isn't allowed");
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // Zero incrementally to avoid too much zeroing in a single call to the TEE.
  const auto& range = heap_range.range();
  zx_status_t status = ZeroSubRangeIncrementally(is_covering_range_explicit,
                                                 range.physical_address(), range.size_bytes());
  if (status != ZX_OK) {
    LOG(ERROR, "ZeroSubRangeIncermentally() failed - status: %d", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::ModifySecureHeapPhysicalRangeInternal(
    fuchsia_sysmem::wire::SecureHeapAndRangeModification range_modification) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(ranges_.size() <= max_range_count_);

  if (!TrySetupSecmemSession()) {
    // Logging in TrySetupSecmemSession
    return ZX_ERR_INTERNAL;
  }

  if (!is_dynamic_checked_) {
    LOG(INFO, "!is_dynamic_checked_");
    return ZX_ERR_BAD_STATE;
  }

  if (!is_dynamic_) {
    LOG(ERROR,
        "ModifySecureHeapPhysicalRangesInternal() can't be called when !dynamic - reply "
        "status: %d",
        ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  if (!ValidateSecureHeapAndRangeModification(range_modification)) {
    // Logging in ValidateSecureHeapAndRangeModification
    return ZX_ERR_INVALID_ARGS;
  }

  const auto& old_range_in = range_modification.old_range();
  const Range old_range =
      Range::BeginLength(old_range_in.physical_address(), old_range_in.size_bytes());
  if (ranges_.find(old_range) == ranges_.end()) {
    LOG(INFO, "ranges_.find(old_range) == ranges_.end()");
    return ZX_ERR_NOT_FOUND;
  }
  const auto& new_range_in = range_modification.new_range();
  const Range new_range =
      Range::BeginLength(new_range_in.physical_address(), new_range_in.size_bytes());

  bool at_start;
  bool longer;
  uint64_t adjustment_magnitude;
  if (old_range.begin() == new_range.begin()) {
    ZX_DEBUG_ASSERT(old_range.end() != new_range.end());
    at_start = false;
    longer = new_range.end() > old_range.end();
    if (longer) {
      adjustment_magnitude = new_range.end() - old_range.end();
    } else {
      adjustment_magnitude = old_range.end() - new_range.end();
    }
  } else {
    ZX_DEBUG_ASSERT(old_range.begin() != new_range.begin());
    at_start = true;
    longer = new_range.begin() < old_range.begin();
    if (longer) {
      adjustment_magnitude = old_range.begin() - new_range.begin();
    } else {
      adjustment_magnitude = new_range.begin() - old_range.begin();
    }
  }

  zx_status_t status =
      AdjustMemoryRange(old_range.begin(), old_range.length(),
                        static_cast<uint32_t>(adjustment_magnitude), at_start, longer);
  if (status != ZX_OK) {
    LOG(INFO, "AdjustMemoryRange() failed - status: %d", status);
    return status;
  }

  ranges_.erase(old_range);
  if (!new_range.empty()) {
    ranges_.insert(new_range);
  }

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::SetupVdec(uint64_t* physical_address, size_t* size_bytes) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(has_attempted_secmem_session_connection_);
  ZX_DEBUG_ASSERT(secmem_session_.has_value());
  uint32_t start;
  uint32_t length;
  TEEC_Result tee_status = secmem_session_->AllocateSecureMemory(&start, &length);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "SecmemSession::AllocateSecureMemory() failed - TEEC_Result %" PRIu32, tee_status);
    return ZX_ERR_INTERNAL;
  }
  *physical_address = start;
  *size_bytes = length;
  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::ProtectMemoryRange(uint64_t physical_address, size_t size_bytes,
                                                      bool enable) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(has_attempted_secmem_session_connection_);
  ZX_DEBUG_ASSERT(secmem_session_.has_value());
  if (!VerifyRange(physical_address, size_bytes, kProtectedRangeGranularity)) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto start = static_cast<uint32_t>(physical_address);
  auto length = static_cast<uint32_t>(size_bytes);
  TEEC_Result tee_status = secmem_session_->ProtectMemoryRange(start, length, enable);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "SecmemSession::ProtectMemoryRange() failed - TEEC_Result %d returning status: %d",
        tee_status, ZX_ERR_INTERNAL);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::AdjustMemoryRange(uint64_t physical_address, size_t size_bytes,
                                                     uint32_t adjustment_magnitude, bool at_start,
                                                     bool longer) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(has_attempted_secmem_session_connection_);
  ZX_DEBUG_ASSERT(secmem_session_.has_value());
  if (!VerifyRange(physical_address, size_bytes, kProtectedRangeGranularity)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!longer && adjustment_magnitude > size_bytes) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (longer) {
    if (at_start) {
      if (!safemath::CheckSub(physical_address, adjustment_magnitude).IsValid<uint32_t>()) {
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      // physical_address + size_bytes was already checked by calling VerifyRange() above.
      if (!safemath::CheckAdd(physical_address + size_bytes, adjustment_magnitude)
               .IsValid<uint32_t>()) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }
  auto start = static_cast<uint32_t>(physical_address);
  auto length = static_cast<uint32_t>(size_bytes);
  TEEC_Result tee_status =
      secmem_session_->AdjustMemoryRange(start, length, adjustment_magnitude, at_start, longer);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "SecmemSession::AdjustMemoryRange() failed - TEEC_Result %d returning status: %d",
        tee_status, ZX_ERR_INTERNAL);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t SysmemSecureMemServer::ZeroSubRangeIncrementally(bool is_covering_range_explicit,
                                                             uint64_t physical_address,
                                                             size_t size_bytes) {
  ZX_DEBUG_ASSERT(thrd_current() == loop_thread_);
  ZX_DEBUG_ASSERT(has_attempted_secmem_session_connection_);
  ZX_DEBUG_ASSERT(secmem_session_.has_value());
  if (!VerifyRange(physical_address, size_bytes, zx_system_get_page_size())) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto start = static_cast<uint32_t>(physical_address);
  auto length = static_cast<uint32_t>(size_bytes);
  // This zeroes incrementally.
  TEEC_Result tee_status = secmem_session_->ZeroSubRange(is_covering_range_explicit, start, length);
  if (tee_status != TEEC_SUCCESS) {
    LOG(ERROR, "SecmemSession::ZeroSubRange() failed - TEEC_Result %d returning status: %d",
        tee_status, ZX_ERR_INTERNAL);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

bool SysmemSecureMemServer::IsOverlap(const Range& a, const Range& b) {
  if (a.end() <= b.begin()) {
    return false;
  }
  if (b.end() <= a.begin()) {
    return false;
  }
  return true;
}

std::pair<SysmemSecureMemServer::Range, SysmemSecureMemServer::Range>
SysmemSecureMemServer::SubtractRanges(const SysmemSecureMemServer::Range& a,
                                      const SysmemSecureMemServer::Range& b) {
  // Caller must ensure this.
  ZX_DEBUG_ASSERT(IsOverlap(a, b));
  Range leftover_left = Range::BeginLength(a.begin(), 0);
  Range leftover_right = Range::BeginLength(a.end(), 0);
  if (b.begin() > a.begin()) {
    leftover_left = Range::BeginEnd(a.begin(), b.begin());
  }
  if (b.end() < a.end()) {
    leftover_right = Range::BeginEnd(b.end(), a.end());
  }
  return std::make_pair(leftover_left, leftover_right);
}
