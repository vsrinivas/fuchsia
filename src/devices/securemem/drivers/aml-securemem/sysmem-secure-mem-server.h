// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/fdf/dispatcher.h>
#include <threads.h>

#include <optional>
#include <set>

#include <tee-client-api/tee_client_api.h>

#include "secmem-session.h"

// This is used with fidl::BindSingleInFlightOnly() to dispatch fuchsia::sysmem::Tee requests.
class SysmemSecureMemServer : public fidl::WireServer<fuchsia_sysmem::SecureMem> {
 public:
  using SecureMemServerDone = fit::callback<void(bool is_success)>;

  SysmemSecureMemServer(const fdf_dispatcher_t* fdf_dispatcher, zx::channel loopback_tee_client);
  ~SysmemSecureMemServer() override;

  zx_status_t BindAsync(zx::channel sysmem_secure_mem_server,
                        thrd_t* sysmem_secure_mem_server_thread,
                        SecureMemServerDone secure_mem_server_done);
  void StopAsync();

  // fidl::WireServer<fuchsia_sysmem::SecureMem> impl
  void GetPhysicalSecureHeaps(GetPhysicalSecureHeapsRequestView request,
                              GetPhysicalSecureHeapsCompleter::Sync& completer) override;
  void GetPhysicalSecureHeapProperties(
      GetPhysicalSecureHeapPropertiesRequestView request,
      GetPhysicalSecureHeapPropertiesCompleter::Sync& completer) override;
  void AddSecureHeapPhysicalRange(AddSecureHeapPhysicalRangeRequestView request,
                                  AddSecureHeapPhysicalRangeCompleter::Sync& completer) override;
  void DeleteSecureHeapPhysicalRange(
      DeleteSecureHeapPhysicalRangeRequestView request,
      DeleteSecureHeapPhysicalRangeCompleter::Sync& completer) override;
  void ModifySecureHeapPhysicalRange(
      ModifySecureHeapPhysicalRangeRequestView request,
      ModifySecureHeapPhysicalRangeCompleter::Sync& completer) override;
  void ZeroSubRange(ZeroSubRangeRequestView request,
                    ZeroSubRangeCompleter::Sync& completer) override;

 private:
  // We might want to extract the Range class into a lib used by aml-securemem and sysmem instead of
  // using very similar code.
  class Range {
   public:
    Range(const Range& to_copy) = default;
    Range& operator=(const Range& to_copy) = default;

    static Range BeginLength(uint64_t begin, uint64_t length) { return Range{begin, length}; }
    static Range BeginEnd(uint64_t begin, uint64_t end) { return Range{begin, end - begin}; }
    uint64_t begin() const { return begin_; }
    uint64_t end() const { return begin_ + length_; }
    uint64_t length() const { return length_; }
    bool empty() const { return end() <= begin(); }

   private:
    explicit Range(uint64_t begin, uint64_t length) : begin_(begin), length_(length) {}
    uint64_t begin_;
    uint64_t length_;
  };

  class CompareRangeByBegin {
   public:
    bool operator()(const Range& left, const Range& right) const {
      // Non-empty is required.
      ZX_DEBUG_ASSERT(left.begin() != left.end());
      ZX_DEBUG_ASSERT(right.begin() != right.end());

      if (left.begin() < right.begin()) {
        // <
        return true;
      } else if (left.begin() > right.begin()) {
        // >
        return false;
      }

      if (left.end() < right.end()) {
        // <
        return true;
      } else if (left.end() > right.end()) {
        // >
        return false;
      }

      // ==
      return false;
    }
  };

  using Ranges = std::set<Range, CompareRangeByBegin>;

  void PostToLoop(fit::closure to_run);

  bool TrySetupSecmemSession();
  void EnsureLoopDone(bool is_success);

  zx_status_t GetPhysicalSecureHeapsInternal(
      fidl::AnyArena* allocator, fuchsia_sysmem::wire::SecureHeapsAndRanges* heaps_and_ranges);
  zx_status_t GetPhysicalSecureHeapPropertiesInternal(
      fuchsia_sysmem::wire::HeapType heap, fidl::AnyArena& allocator,
      fuchsia_sysmem::wire::SecureHeapProperties* properties);
  zx_status_t AddSecureHeapPhysicalRangeInternal(
      fuchsia_sysmem::wire::SecureHeapAndRange heap_range);
  zx_status_t DeleteSecureHeapPhysicalRangeInternal(
      fuchsia_sysmem::wire::SecureHeapAndRange heap_range);
  zx_status_t ModifySecureHeapPhysicalRangeInternal(
      fuchsia_sysmem::wire::SecureHeapAndRangeModification range_modification);
  zx_status_t ZeroSubRangeInternal(bool is_covering_range_explicit,
                                   fuchsia_sysmem::wire::SecureHeapAndRange heap_range);

  // Call secmem TA to setup the one physical secure heap that's configured by the TEE Controller.
  zx_status_t SetupVdec(uint64_t* physical_address, uint64_t* size_bytes);

  // Call secmem TA to setup the one physical secure heap that's configured by sysmem.
  zx_status_t ProtectMemoryRange(uint64_t physical_address, uint64_t size_bytes, bool enable);
  zx_status_t AdjustMemoryRange(uint64_t physical_address, size_t size_bytes,
                                uint32_t adjustment_magnitude, bool at_start, bool longer);
  zx_status_t ZeroSubRangeIncrementally(bool is_covering_range_explicit, uint64_t physical_address,
                                        size_t size_bytes);

  static bool IsOverlap(const Range& a, const Range& b);
  static std::pair<Range, Range> SubtractRanges(const Range& a, const Range& b);

  const fdf_dispatcher_t* fdf_dispatcher_ = nullptr;
  fuchsia::tee::ApplicationSyncPtr tee_connection_ = {};
  async::Loop loop_;
  thrd_t loop_thread_ = {};
  bool was_thread_started_ = {};
  bool is_loop_done_ = {};
  SecureMemServerDone secure_mem_server_done_;

  bool is_get_physical_secure_heaps_called_ = {};

  bool is_dynamic_checked_ = {};
  bool is_dynamic_ = {};
  uint32_t max_range_count_ = 0;

  // We try to open a SecmemSession once.  If that fails, we remember the status and
  // EnsureSecmemSession() will return that status without trying Init() again.
  bool has_attempted_secmem_session_connection_ = false;
  std::optional<SecmemSession> secmem_session_ = std::nullopt;

  Ranges ranges_;

  // Last on purpose.
  ClosureQueue closure_queue_;
};

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_SYSMEM_SECURE_MEM_SERVER_H_
