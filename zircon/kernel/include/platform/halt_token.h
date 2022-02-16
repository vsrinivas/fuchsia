// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_TOKEN_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_TOKEN_H_

#include <zircon/errors.h>

#include <kernel/event.h>
#include <kernel/thread_lock.h>
#include <ktl/atomic.h>

// This object is used to coordinate concurrent halt/reboot operations.
//
// The idea is there's a single resource, the "halt token" and only the holder of the token may
// initiate a halt/reboot (except for panics).
//
class HaltToken {
 public:
  // Accessor for the global singleton halt token.
  static HaltToken& Get() { return g_instance; }

  // The Take method attempts to acquire the token and signals an irrevocable
  // intention to halt (or reboot) the system.
  //
  // If this method returns true, the caller has acquired the token and is now
  // responsible for halting/reboot.
  //
  // If this method returns false, the caller failed to acquire the token
  // (because some other caller got it).  In this case the caller must take no
  // action and allow the holder to halt/reboot.
  [[nodiscard]] bool Take() { return !halt_token_claimed_.exchange(true); }

  // Wait until |deadline| for user-mode to acknowledge a kernel-signaled Halt.
  // In practice, this occurs when the kernel memory watchdog encounters a fatal
  // OOM condition and signals user mode, in order to give it a last chance to
  // persist logs and cleanly shutdown drivers before the reboot actually takes
  // place.
  zx_status_t WaitForAck(const Deadline& deadline) { return ack_event_.Wait(deadline); }

  // Called during processing of the
  // ZX_SYSTEM_POWERCTL_ACK_KERNEL_INITIATED_REBOOT topic in zx_system_powerctl.
  // Indicates that user-mode has finished responding to the kernel's signal of
  // an impending reboot, and that user-mode is now ready for the reboot to
  // proceed.
  //
  // If the halt token has not yet been claimed, this function will return an
  // error and leave the ack_event_ in the unsignaled state.
  zx_status_t AckPendingHalt() TA_EXCL(thread_lock) {
    if (!halt_token_claimed_.load()) {
      return ZX_ERR_BAD_STATE;
    }
    ack_event_.Signal();
    return ZX_OK;
  }

 private:
  // No public construction.  Global singleton only.
  HaltToken() = default;

  // No copy or move.
  HaltToken(const HaltToken&) = delete;
  HaltToken(HaltToken&&) = delete;
  HaltToken& operator=(const HaltToken&) = delete;
  HaltToken& operator=(HaltToken&&) = delete;

  static HaltToken g_instance;

  ktl::atomic<bool> halt_token_claimed_{false};
  Event ack_event_{false};
};

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_HALT_TOKEN_H_
