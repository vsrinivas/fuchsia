// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_PERSISTENT_DEBUGLOG_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_PERSISTENT_DEBUGLOG_INTERNAL_H_

#include <lib/persistent-debuglog.h>
#include <stdio.h>

#include <arch/ops.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <ktl/string_view.h>

namespace tests {
struct PersistentDebuglogTestingFriend;
}

class PersistentDebugLog {
 public:
  PersistentDebugLog(char* recovered_data, uint32_t recovered_capacity)
      : recovered_persistent_log_(recovered_data, recovered_capacity) {}

  // Called very early in boot.  Attempts to recover any previously persisted
  // log by first performing consistency checks on the header, and then
  // recovering as much as it can into the recovery buffer.
  void SetLocation(void* virt, size_t len);
  void Write(ktl::string_view str);
  ktl::string_view GetRecoveredLog() const {
    const auto& rpl = recovered_persistent_log_;
    return (rpl.size == 0) ? ktl::string_view{nullptr, 0} : ktl::string_view{rpl.data, rpl.size};
  }

  void Invalidate() {
    Guard<SpinLock, IrqSave> guard{&persistent_log_lock_};
    if (plog.hdr) {
      plog.hdr->InvalidateMagic();
    }
  }

 private:
  friend struct ::tests::PersistentDebuglogTestingFriend;

  struct LogHeader {
    static constexpr uint32_t kMagic =
        (static_cast<uint32_t>('P') << 0) | (static_cast<uint32_t>('l') << 8) |
        (static_cast<uint32_t>('o') << 16) | (static_cast<uint32_t>('g') << 24);

    char* payload() { return reinterpret_cast<char*>(this + 1); }
    bool IsMagicValid() const { return magic == kMagic; }

    void ValidateMagic() {
      magic = kMagic;
      CleanCacheRange(this, sizeof(*this));
    }

    void InvalidateMagic() {
      magic = 0;
      CleanCacheRange(this, sizeof(*this));
    }

    uint32_t magic;
    uint32_t rd_ptr;
  } __PACKED;

  static inline void CleanCacheRange(void* addr, size_t len) {
    arch_clean_cache_range(reinterpret_cast<vaddr_t>(addr), len);
  }

  // Used only by testing to reset a log to its "pre SetLocation'ed" state.
  void ForceReset() {
    Guard<SpinLock, IrqSave> guard{&persistent_log_lock_};
    plog.hdr = nullptr;
    plog.payload_size = 0;
    recovered_persistent_log_.size = 0;
  }

  DECLARE_SPINLOCK(PersistentDebugLog) persistent_log_lock_;

  // Info about where the persisted log is in RAM, if we have a persisted log.
  struct {
    LogHeader* hdr = nullptr;
    uint32_t payload_size = 0;
  } plog TA_GUARDED(persistent_log_lock_);

  // We don't bother to lock this structure.  The log, if present, is recovered
  // during early boot while we are still running on a single core.  After that,
  // the recovered data is only ever accessed in a read only fashion, so there
  // is no real need to provide any explicit synchronization.
  struct RecoveredPersistentLog {
    constexpr RecoveredPersistentLog(char* _data, uint32_t _capacity)
        : data(_data), capacity(_capacity) {}

    char* const data;
    const uint32_t capacity;
    uint32_t size = 0;
  } recovered_persistent_log_;
};

#endif  // ZIRCON_KERNEL_LIB_PERSISTENT_DEBUGLOG_PERSISTENT_DEBUGLOG_INTERNAL_H_
