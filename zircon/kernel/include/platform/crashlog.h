// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_

#include <stdio.h>
#include <sys/types.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <ktl/atomic.h>
#include <ktl/span.h>

class PlatformCrashlog {
 public:
  // This is common interface definition for specific implementations of crashlog support.
  // Depending on the platform, and the resources provided to it, users will end up with
  //
  // 1) A RAM mappable crashlog implementation, where the crashlog RAM is mapped
  //    directly accessible by the CPUs from a mapped virtual address.
  // 2) An EFI capsule based implementation, where crashlogs get stored in an
  //    EFI capsule during a non-spontaneous crash.
  // 3) A trivial, no-op implementation, because the kernel was not provided any
  //    way to store data which could survive a reboot.
  //
  // Generally speaking, the underlying implementation are not multi-thread
  // safe, and users should take care to never do something like having multiple
  // threads calling into Finalize at the same time.
  class Interface {
   public:
    // Returns a span which points to the region which the kernel should render
    // a crashlog payload to during a non-spontaneous crash.
    virtual ktl::span<char> GetRenderTarget() = 0;

    // Finalize a crashlog just before triggering a reboot.  |reason| is the SW
    // reboot reason which will be stored in the crashlog header, while |amt| is
    // the amount of the render target (see |GetRenderTarget|) which was filled
    // before calling finalize.
    virtual void Finalize(zircon_crash_reason_t reason, size_t amt) = 0;

    // Attempt to recover any crashlog from platform specific storage, writing
    // the results to |tgt|.  Returns the number of bytes written to |tgt| in
    // the process.  Users may pass nullptr to |tgt| if they wish to simply
    // measure the size of the crashlog to be recovered.
    virtual size_t Recover(FILE* tgt) = 0;

    // Enable periodic updates of the uptime estimate in the crashlog header.
    // This allows systems with directly mappable crashlog RAM to constantly be
    // stashing a valid header with an uptime estimate and a reboot reason of
    // "UNKNOWN" to be recovered in the case of a spontaneous reboot.
    virtual void EnableCrashlogUptimeUpdates(bool enabled) = 0;

   protected:
    // No one should ever be destroying an implementation of the crashlog
    // interface from an Interface* pointer.  Bury the destructor as a
    // protected, non-virtual destructor to prevent anyone from ever
    // accidentally attempting to do so.
    ~Interface() = default;
  };

  // Interface management.
  //
  // By default, there is always a Crashlog implementation available, and a
  // reference to it can be fetched using the |Get| method.  At boot, however,
  // this will be a default trivial implementation which does nothing.  The
  // system may switch away from this interface when it discovers usable
  // non-volatile storage by calling |Bind|, and passing a reference to a
  // specific Crashlog implementation. Note that this implementation must stay
  // alive for the entire life of the kernel.  Once bound to a non-trivial
  // implementation, the interface can no longer be rebound, and any attempts to
  // do so will return an error.
  //
  // Code may check to see if a non-trivial implementation has been bound by
  // calling |HasNonTrivialImpl|.
  static Interface& Get() { return *(interface_.load(ktl::memory_order_acquire)); }

  static bool HasNonTrivialImpl() {
    return interface_.load(ktl::memory_order_acquire) != &trivial_impl_;
  }

  static zx_status_t Bind(Interface& impl) {
    Interface* expected = &trivial_impl_;
    return interface_.compare_exchange_strong(expected, &impl, ktl::memory_order_acq_rel)
               ? ZX_OK
               : ZX_ERR_ALREADY_BOUND;
  }

 private:
  class TrivialImpl : public Interface {
   public:
    constexpr TrivialImpl() = default;
    ~TrivialImpl() = default;

    ktl::span<char> GetRenderTarget() final { return {}; }
    void Finalize(zircon_crash_reason_t reason, size_t amt) final {}
    size_t Recover(FILE* tgt) final { return 0; }
    void EnableCrashlogUptimeUpdates(bool enabled) final {}
  };

  // No one should be creating or destroying any PlatformCrashlog instances.
  // Everything should be going through a PlatformCrashlog::Interface instead.
  PlatformCrashlog() = default;
  ~PlatformCrashlog() = default;

  // The singleton interface.  This can be set to a non-trivial implementation
  // only once via Bind.
  static inline TrivialImpl trivial_impl_;
  static inline ktl::atomic<Interface*> interface_{&trivial_impl_};
};

void platform_set_ram_crashlog_location(paddr_t phys, size_t len);
bool platform_has_ram_crashlog();

/* Stash the crashlog somewhere platform-specific that allows
 * for recovery after reboot.  This will only be called out
 * of the panic() handling path on the way to reboot, and is
 * not necessarily safe to be called from any other state.
 *
 * Calling with a NULL log returns the maximum supported size.
 * It is safe to query the size at any time after boot.  If the
 * return is 0, no crashlog recovery is supported.
 */
extern void (*platform_stow_crashlog)(zircon_crash_reason_t reason, const void* log, size_t len);

/* Recover the crashlog, fprintf'ing its contents into the FILE |tgt|
 * provided by the caller, then return the length of the recovered
 * crashlog.
 *
 * It is safe to call this function more than once.  Users may compute
 * the length of the crashlog without rendering it by passing nullptr
 * for |tgt|.  The length of the rendered log is guaranteed to stay
 * constant between calls.
 *
 */
extern size_t (*platform_recover_crashlog)(FILE* tgt);

/* Either enable or disable periodic updates of the crashlog uptime. */
extern void (*platform_enable_crashlog_uptime_updates)(bool enabled);

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_CRASHLOG_H_
