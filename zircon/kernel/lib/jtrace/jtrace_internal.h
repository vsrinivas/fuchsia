// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_JTRACE_JTRACE_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_JTRACE_JTRACE_INTERNAL_H_

#include <lib/affine/ratio.h>
#include <lib/fit/defer.h>
#include <lib/jtrace/jtrace.h>
#include <platform.h>
#include <zircon/time.h>

#include <arch/ops.h>
#include <fbl/canary.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/cpu.h>
#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <ktl/limits.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <pretty/hexdump.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

namespace jtrace {

// fwd decl of our tests structure.  This allows the JTrace class to be friends with the tests.
struct tests;

// A small utility function, used only locally.
// TODO(johngro): Add this to ktl?
template <typename T, typename U>
static inline constexpr T saturate(U val) {
  return (val > ktl::numeric_limits<T>::max())   ? ktl::numeric_limits<T>::max()
         : (val < ktl::numeric_limits<T>::min()) ? ktl::numeric_limits<T>::min()
                                                 : static_cast<T>(val);
}

// The definition of an interface used to abstract printing operations used when
// dumping trace buffers. This implementation is replaced during testing in
// order to verify that trace dumps are working as expected.
class TraceHooks {
 public:
  TraceHooks() = default;
  virtual ~TraceHooks() = default;

  virtual void PrintWarning(const char* fmt, ...) __PRINTFLIKE(2, 3) = 0;
  virtual void PrintInfo(const char* fmt, ...) __PRINTFLIKE(2, 3) = 0;
  virtual void Hexdump(const void* data, size_t size) = 0;
  virtual void PerCpuDumpStarted() {}
  virtual void PrintTraceEntry(const Entry<UseLargeEntries::Yes>& e, TraceBufferType buf_type,
                               zx_time_t ts, zx_duration_t delta = 0) = 0;
  virtual void PrintTraceEntry(const Entry<UseLargeEntries::No>& e, TraceBufferType buf_type,
                               zx_time_t ts, zx_duration_t delta = 0) = 0;
};

// A traits struct which holds the configuration of an instance of the debug trace subsystem.
template <size_t _kTargetBufferSize, size_t _kLastEntryStorage,
          ::jtrace::IsPersistent _kIsPersistent, ::jtrace::UseLargeEntries _kUseLargeEntries>
struct Config {
  static constexpr size_t kTargetBufferSize = _kTargetBufferSize;
  static constexpr size_t kLastEntryStorage = _kLastEntryStorage;
  static constexpr ::jtrace::IsPersistent kIsPersistent = _kIsPersistent;
  static constexpr ::jtrace::UseLargeEntries kUseLargeEntries = _kUseLargeEntries;
  using Entry = ::jtrace::Entry<kUseLargeEntries>;
};

template <typename Config>
struct Header {
  using Entry = typename Config::Entry;

  static constexpr uint32_t kNoMagic = 0;
  static constexpr uint32_t kMagic = fbl::magic("Jtrc");

  uint32_t magic{kMagic};
  ktl::atomic<uint32_t> wr{0};
  Entry last_cpu_entries_[Config::kLastEntryStorage];
  Entry entries[];
};

template <typename Config>
class JTrace {
 public:
  using Entry = typename Config::Entry;
  using Header = ::jtrace::Header<Config>;

  JTrace(TraceHooks& hooks) : hooks_(hooks) {}

  // No copy, no move.
  JTrace(const JTrace&) = delete;
  JTrace& operator=(const JTrace&) = delete;
  JTrace(JTrace&&) = delete;
  JTrace& operator=(JTrace&&) = delete;

  void SetLocation(ktl::span<uint8_t> storage) {
    // The location of the trace buffer should only ever get set once, either
    // during the init call for a non-persistent log, or later on during the
    // allocation of persistent RAM during ZBI processing.  If an attempt is
    // made to set the location twice, simply ignore it.  Do not attempt to
    // debug assert, as we are very likely to be so early in boot that it would
    // be very difficult to debug such an assert.
    if ((storage_.data() != nullptr) || !storage_.empty()) {
      return;
    }

    // Reject the buffer if it is null/empty for any reason, or if the pointer
    // provided does not meet the alignment requirements of our header.
    if ((storage.data() == nullptr) || storage.empty() ||
        (reinterpret_cast<uintptr_t>(storage.data()) % alignof(decltype(*hdr())))) {
      return;
    }

    // If the supplied storage is not enough to hold the header and even a
    // single trace entry, then just disable debug tracing by setting the header
    // location and the number of entries to nothing.  In theory, these should
    // already be nullptr and 0, but we explicitly set them anyway since (again)
    // ASSERTing is not really an option given how early on in boot we are
    // likely to be right now.
    if (storage.size() < sizeof(Header) + sizeof(Entry)) {
      return;
    }

    // If this is a persistent trace and we have a recovery buffer, attempt to
    // recover the log from our storage before proceeding to re-init our trace
    // storage.
    storage_ = storage;
    if constexpr (kRecoveryBufferSize > 0) {
      ::memcpy(recovered_buf_, storage_.data(), ktl::min(sizeof(recovered_buf_), storage_.size()));
    }

    // Go ahead and initialize our header and the number of entries we can hold
    // in our log, then flush this out to physical RAM if this is a persistent
    // trace buffer.
    new (storage_.data()) Header();
    entry_cnt_ = static_cast<uint32_t>((saturate<uint32_t>(storage_.size()) - sizeof(Header)) /
                                       sizeof(Entry));
    CleanCache(storage_.data(), storage_.size());
  }

  ktl::span<uint8_t> GetLocation() const { return storage_; }

  void Invalidate() {
    if (hdr() != nullptr) {
      hdr()->magic = Header::kNoMagic;
      CleanCache(hdr(), sizeof(*hdr()));
    }
  }

  void Log(Entry& entry) {
    if (hdr() == nullptr) {
      return;
    }

    // Record the fact that there is now a trace operation in progress, making
    // sure that we remember to decrement this count when we exit this method.
    trace_ops_in_flight_.fetch_add(1, ktl::memory_order_acq_rel);
    auto cleanup =
        fit::defer([this]() { trace_ops_in_flight_.fetch_add(-1, ktl::memory_order_acq_rel); });

    // Try to reserve a slot to write a record into.  This should only fail if
    // the tracing is temporarily disabled for dumping.
    ktl::optional<uint32_t> opt_wr = ReserveSlot();
    if (!opt_wr.has_value()) {
      return;
    }

    // Go ahead and finish filling out the entry, then copy the entry into the
    // main trace buffer, and (if enabled) into the per-cpu last entry slot in
    // the header.  Please note 2 potentially subtle points about this:
    //
    // 1) We mutate the log entry passed to us by the caller to finish filling
    //    it out, then record that entry in both places.  It is important that
    //    we don't copy the record into the main trace buffer and _then_ finish
    //    filling out the record (copying it from the main trace buffer to the
    //    per-cpu header slot afterwards).  There is a small chance that between
    //    these operations, the log ends up wrapping and our currently assigned
    //    slot gets stomped (causing the per-cpu record to be corrupted).
    // 2) We need to disable preemption for the time period between when we
    //    record the CPU ID in the trace entry, and when write that entry into
    //    the per-cpu slot.  Failure to do this could result in us recording CPU
    //    X in the entry, but then being moved to CPU Y before writing to the
    //    per-cpu slot for X (and setting up a potential race between us and
    //    whoever is running on CPU X now).
    //
    const uint32_t wr = opt_wr.value();
    {
      AutoPreemptDisabler preempt_disabler;
      entry.ts_ticks = current_ticks();
      entry.cpu_id = arch_curr_cpu_num();
      if constexpr (Config::kUseLargeEntries == UseLargeEntries::Yes) {
        entry.tid = Thread::Current::Get()->tid();
      }

      hdr()->entries[wr] = entry;

      if constexpr (Config::kLastEntryStorage > 0) {
        if (entry.cpu_id < ktl::size(hdr()->last_cpu_entries_)) {
          hdr()->last_cpu_entries_[entry.cpu_id] = entry;
        }
      }
    }

    // Flush the header and the entry if this is a persistent trace.
    CleanCache(hdr(), sizeof(*hdr()));
    CleanCache(hdr()->entries + wr, sizeof(Entry));
  }

  // |timeout| controls how long this method will wait (spin) for other threads
  // to complete in progress writes before continuing on and dumping the buffer.
  void Dump(zx_duration_t timeout) {
    if (hdr() == nullptr) {
      hooks_.PrintWarning("No debug trace buffer was ever configured\n");
      return;
    }

    // Disable tracing and give any thread currently in the process of writing a
    // record some time to get out of the way.  Note that this is a best effort
    // approach to disabling the tracing during our dump.  It is important that
    // debug tracing remain lockless at all times.  The worst case scenario here
    // is that we end up with a partially garbled trace record.
    SetTraceEnabled(false);

    const affine::Ratio ticks_to_mono_ratio = platform_get_ticks_to_time_ratio();
    const zx_ticks_t deadline =
        zx_ticks_add_ticks(current_ticks(), ticks_to_mono_ratio.Inverse().Scale(timeout));
    while ((trace_ops_in_flight_.load(ktl::memory_order_acquire) > 0) &&
           (current_ticks() < deadline)) {
      // just spin while we wait.
      arch::Yield();
    }

    // Print a warning if we never saw the in flight op-count hit zero, then go
    // ahead and dump the current buffer.
    if (current_ticks() >= deadline) {
      hooks_.PrintWarning(
          "Warning: ops in flight was never observed at zero while waiting to dump the current "
          "trace buffer.  Some trace records might be corrupt.\n");
    }

    ktl::span<const uint8_t> data{reinterpret_cast<const uint8_t*>(hdr()),
                                  sizeof(*hdr()) + (sizeof(Entry) * entry_cnt_)};
    Dump(data, TraceBufferType::Current);

    // Finally, go ahead and re-enable tracing.
    SetTraceEnabled(true);
  }

  void DumpRecovered() {
    if constexpr (kRecoveryBufferSize > 0) {
      Dump({recovered_buf_, sizeof(recovered_buf_)}, TraceBufferType::Recovered);
    } else {
      hooks_.PrintWarning(
          "Debug tracing is not configured for persistent tracing.  There is no recovered buffer "
          "to dump.\n");
    }
  }

 private:
  // Tests are allowed to peek at our private state.
  friend struct ::jtrace::tests;

  static constexpr uint32_t kTraceDisabledFlag = 0x80000000;
  static constexpr uint32_t kRecoveryBufferSize =
      (Config::kIsPersistent == jtrace::IsPersistent::Yes) ? Config::kTargetBufferSize : 0;

  static inline void CleanCache(void* ptr, size_t len) {
    // Trace data only needs to be flushed to physical RAM if the trace is meant
    // to be persistent.
    if constexpr (Config::kIsPersistent == jtrace::IsPersistent::Yes) {
      arch_clean_cache_range(reinterpret_cast<vaddr_t>(ptr), len);
    }
  }

  inline Header* hdr() { return reinterpret_cast<Header*>(storage_.data()); }

  ktl::optional<uint32_t> ReserveSlot() {
    uint32_t wr, next_wr;
    do {
      wr = hdr()->wr.load(ktl::memory_order_acquire);

      if (wr & kTraceDisabledFlag) {
        return ktl::nullopt;
      }

      next_wr = (wr + 1) % entry_cnt_;
    } while (!hdr()->wr.compare_exchange_weak(wr, next_wr, ktl::memory_order_acq_rel));

    return wr;
  }

  void SetTraceEnabled(bool enabled) {
    uint32_t wr, next_wr;
    do {
      wr = hdr()->wr.load(ktl::memory_order_acquire);
      next_wr = enabled ? (wr & ~kTraceDisabledFlag) : (wr | kTraceDisabledFlag);
    } while (!hdr()->wr.compare_exchange_weak(wr, next_wr));
  }

  void Dump(ktl::span<const uint8_t> buf, TraceBufferType buf_type) {
    auto dump_corrupted_log = fit::defer([this, buf]() {
      hooks_.PrintWarning("JTRACE: Dumping corrupted log\n");
      hooks_.Hexdump(buf.data(), buf.size());
    });

    const size_t total_size = (sizeof(*hdr()) + (entry_cnt_ * sizeof(Entry)));
    if (total_size > buf.size()) {
      hooks_.PrintWarning("JTRACE: recovery buffer too small (%zu) to hold %u entries\n",
                          buf.size(), entry_cnt_);
      return;
    }

    const Header* hdr = reinterpret_cast<const Header*>(buf.data());
    const Entry* entries = hdr->entries;
    if (hdr->magic == Header::kNoMagic) {
      hooks_.PrintInfo("JTRACE: Log appears clean, not dumping it (recoverd %p main %p len %zu)\n",
                       buf.data(), this->hdr(), total_size);
      dump_corrupted_log.cancel();
      return;
    }

    const uint32_t wr = hdr->wr.load() & ~kTraceDisabledFlag;
    if ((hdr->magic != Header::kMagic) || (wr >= entry_cnt_)) {
      hooks_.PrintWarning("JTRACE: Bad header: Magic 0x%08x Wr %u Entries %u\n", hdr->magic,
                          hdr->wr.load(), entry_cnt_);
      return;
    }

    dump_corrupted_log.cancel();

    // Figure out how many entries we will dump, and where to start dumping
    // from.  We skip any initial entries which have a timestamp of zero.  Most
    // likely, they were entries which were never written to because the trace
    // never wrapped.
    uint32_t rd = wr;
    uint32_t todo = entry_cnt_;
    while (todo) {
      const auto& e = entries[rd];
      if (e.ts_ticks != 0) {
        break;
      }
      rd = (rd + 1) % entry_cnt_;
      --todo;
    }

    if (todo) {
      hooks_.PrintInfo("JTRACE: Recovered %u/%u entries\n", todo, entry_cnt_);
      const affine::Ratio ticks_to_mono_ratio = platform_get_ticks_to_time_ratio();
      zx_time_t prev_ts = ticks_to_mono_ratio.Scale(entries[rd].ts_ticks);

      for (; todo != 0; --todo) {
        const auto& e = entries[rd];
        const zx_time_t ts = ticks_to_mono_ratio.Scale(entries[rd].ts_ticks);

        hooks_.PrintTraceEntry(e, buf_type, ts, ts - prev_ts);
        prev_ts = ts;
        rd = (rd + 1) % entry_cnt_;
      }

      // If we are configured to track per-cpu last events, go ahead and print
      // them out.
      if constexpr (Config::kLastEntryStorage > 0) {
        hooks_.PerCpuDumpStarted();
        hooks_.PrintInfo("\n");
        hooks_.PrintInfo("JTRACE: Last recorded per-CPU events.\n");

        if (Config::kLastEntryStorage != arch_max_num_cpus()) {
          hooks_.PrintWarning(
              "JTRACE: Warning! Configured per-cpu last entry count (%zu) does not match target's "
              "number of CPUs (%u)\n",
              Config::kLastEntryStorage, arch_max_num_cpus());
        }

        for (const Entry& e : hdr->last_cpu_entries_) {
          zx_time_t ts = ticks_to_mono_ratio.Scale(e.ts_ticks);
          hooks_.PrintTraceEntry(e, buf_type, ts);
        }
      }

      const int64_t last_sec = prev_ts / ZX_SEC(1);
      const int64_t last_nsec = prev_ts % ZX_SEC(1);
      hooks_.PrintInfo("\n");
      hooks_.PrintInfo("JTRACE: Last log timestamp [%4ld.%09ld]\n", last_sec, last_nsec);
    } else {
      hooks_.PrintInfo("JTRACE: no entries\n");
    }
  }

  TraceHooks& hooks_;
  ktl::span<uint8_t> storage_{};
  uint32_t entry_cnt_{0};
  ktl::atomic<uint32_t> trace_ops_in_flight_{0};
  alignas(Header) uint8_t recovered_buf_[kRecoveryBufferSize];
};

}  // namespace jtrace

#endif  // ZIRCON_KERNEL_LIB_JTRACE_JTRACE_INTERNAL_H_
