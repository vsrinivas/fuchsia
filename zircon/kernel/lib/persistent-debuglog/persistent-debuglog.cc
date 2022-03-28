// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ctype.h>
#include <lib/console.h>
#include <lib/lazy_init/lazy_init.h>
#include <stdio.h>

#include <ktl/algorithm.h>
#include <ktl/bit.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/type_traits.h>

#include "persistent-debuglog-internal.h"

#include <ktl/enforce.h>

namespace {

template <size_t kStorageSize>
class PersistentDebuglogGlobals {
 public:
  void init_early() {
    auto foo = sizeof(storage_);
    static_assert(ktl::is_same_v<decltype(foo), size_t>);
    static_assert(static_cast<size_t>(sizeof(storage_)) <= ktl::numeric_limits<uint32_t>::max());
    static_assert(sizeof(storage_) <= static_cast<size_t>(ktl::numeric_limits<uint32_t>::max()));
    static_assert(static_cast<size_t>(sizeof(storage_)) <=
                  static_cast<size_t>(ktl::numeric_limits<uint32_t>::max()));
    instance_.Initialize(storage_, static_cast<uint32_t>(sizeof(storage_)));
  }
  void set_location(void* vaddr, size_t len) { instance_->SetLocation(vaddr, len); }
  void write(const ktl::string_view str) { instance_->Write(str); }
  void invalidate() { instance_->Invalidate(); }
  ktl::string_view get_recovered_log() const { return instance_->GetRecoveredLog(); }

  int cmd(int argc, const cmd_args* argv, uint32_t flags) {
    auto usage = [&]() -> int {
      printf("usage:\n");
      printf("%s dump : dump the recovered persistent debug log\n", argv[0].str);
      return ZX_ERR_INTERNAL;
    };

    if (argc < 2) {
      printf("not enough arguments\n");
      return usage();
    }

    if (!strcmp(argv[1].str, "dump")) {
      ktl::string_view recovered = instance_->GetRecoveredLog();
      if (recovered.size() > 0) {
        printf("Recovered %zu bytes from the persistent debug log.\n", recovered.size());
        printf("---- BEGIN ----\n");
        stdout->Write(recovered);
        printf("---- END ----\n");
      } else {
        printf("There was no persistent debug log recovered!\n");
      }
    } else {
      printf("unknown command\n");
      return usage();
    }

    return ZX_OK;
  }

 private:
  lazy_init::LazyInit<PersistentDebugLog, lazy_init::CheckType::None,
                      lazy_init::Destructor::Disabled>
      instance_;
  char storage_[kStorageSize];
};

template <>
class PersistentDebuglogGlobals<0> {
 public:
  void init_early() {}
  void set_location(void* vaddr, size_t len) {}
  void write(const ktl::string_view str) {}
  void invalidate() {}
  ktl::string_view get_recovered_log() { return ktl::string_view{nullptr, 0}; }
  int cmd(int argc, const cmd_args* argv, uint32_t flags) { return -1; }
};

PersistentDebuglogGlobals<kTargetPersistentDebugLogSize> gLog;

}  // namespace

void PersistentDebugLog::SetLocation(void* virt, size_t len) {
  // The location of the persistent dlog must be compatible with the header's
  // alignment, and the amount of space for the log payload must be positive,
  // otherwise we cannot effectively use the memory.
  static_assert(ktl::has_single_bit(alignof(LogHeader)),
                "Persistent dlog header's alignment must be a power of 2 in size");
  if ((reinterpret_cast<uintptr_t>(virt) & (alignof(LogHeader) - 1)) ||
      (len <= sizeof(LogHeader))) {
    return;
  }

  // If we already have a persistent dlog location, then this function has been
  // (improperly) called twice.  It is tempting to assert here, but we are
  // currently very early in boot, which would make debugging the assert
  // extremely difficult.  Instead, just ignore this request.
  //
  // TODO(johngro): come back here and try to put a warning/OOPS in to the dlog
  // buffer?
  Guard<SpinLock, IrqSave> guard{&persistent_log_lock_};
  if (plog.hdr != nullptr) {
    return;
  }

  // Check our log header to see if we have a recovered persistent log.  If we
  // do, recover the log into our recovery buffer so that it can be picked up
  // later on and sent up to usermode in the crashlog.
  //
  // Note: we are at a point in boot where the heap has not been brought up yet,
  // which is why the recovery buffer needs to be statically allocated.  If this
  // becomes an issue some day, we can shift to a strategy where we remember
  // where the persistent log is, but don't actually start to use it until we
  // get a chance to save it off into a dynamically allocated buffer.
  LogHeader* hdr = reinterpret_cast<LogHeader*>(virt);
  const uint32_t payload_size = static_cast<uint32_t>(
      ktl::min<size_t>(ktl::numeric_limits<uint32_t>::max(), len - sizeof(LogHeader)));
  if (hdr->IsMagicValid() && (hdr->rd_ptr < payload_size)) {
    // This looks as good as it is going to.  Our magic number is valid, and our
    // read pointer lies within the available payload size.  Save as much as we
    // can in our static buffer, discarding the oldest data first if we cannot
    // fit it all.
    uint32_t todo, rd;
    auto& rpl = recovered_persistent_log_;

    if (payload_size <= rpl.capacity) {
      todo = payload_size;
      rd = hdr->rd_ptr;
    } else {
      DEBUG_ASSERT(rpl.capacity <= ktl::numeric_limits<decltype(todo)>::max());
      todo = rpl.capacity;
      rd = (hdr->rd_ptr + (payload_size - todo)) % payload_size;
    }

    char* src = hdr->payload();
    for (rpl.size = 0; todo > 0; --todo) {
      char c = src[rd++];
      if (rd >= payload_size) {
        rd = 0;
      }

      // Skip all zeros.
      if (c == 0) {
        continue;
      }

      rpl.data[rpl.size++] = (isprint(c) || (c == '\n')) ? c : '?';
    }
  }

  // Reset the log, then install it, and we are done.
  hdr->ValidateMagic();
  hdr->rd_ptr = 0;
  ::memset(hdr->payload(), 0, payload_size);
  CleanCacheRange(hdr, len);
  plog.hdr = hdr;
  plog.payload_size = payload_size;
}

void PersistentDebugLog::Write(ktl::string_view str) {
  Guard<SpinLock, IrqSave> guard{&persistent_log_lock_};

  // If we have no persistent log, just get out.
  if (plog.hdr == nullptr) {
    return;
  }

  // Copy the data into our log
  char* payload = plog.hdr->payload();
  uint32_t todo;
  size_t offset;
  if (str.size() > plog.payload_size) {
    todo = plog.payload_size;
    offset = str.size() - plog.payload_size;
  } else {
    todo = static_cast<uint32_t>(str.size());
    offset = 0;
  }

  uint32_t space = plog.payload_size - plog.hdr->rd_ptr;
  if (space > todo) {
    str.copy(payload + plog.hdr->rd_ptr, todo, offset);
    CleanCacheRange(payload + plog.hdr->rd_ptr, todo);

    plog.hdr->rd_ptr += todo;
    CleanCacheRange(plog.hdr, sizeof(*plog.hdr));
  } else {
    str.copy(payload + plog.hdr->rd_ptr, space, offset);
    str.copy(payload, todo - space, offset + space);
    CleanCacheRange(payload + plog.hdr->rd_ptr, space);
    CleanCacheRange(payload, todo - space);

    plog.hdr->rd_ptr = todo - space;
    CleanCacheRange(plog.hdr, sizeof(*plog.hdr));
  }
}

void persistent_dlog_init_early() { gLog.init_early(); }

void persistent_dlog_set_location(void* vaddr, size_t len) { gLog.set_location(vaddr, len); }

void persistent_dlog_write(const ktl::string_view str) { gLog.write(str); }

void persistent_dlog_invalidate() { gLog.invalidate(); }

ktl::string_view persistent_dlog_get_recovered_log() { return gLog.get_recovered_log(); }

#if TARGET_PERSISTENT_DEBUGLOG_SIZE > 0
namespace {
int cmd_pdlog(int argc, const cmd_args* argv, uint32_t flags) {
  return gLog.cmd(argc, argv, flags);
}
STATIC_COMMAND_START
STATIC_COMMAND_MASKED("pdlog", "pdlog", &cmd_pdlog, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(pdlog)
}  // namespace
#endif
