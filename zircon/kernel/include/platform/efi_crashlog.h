// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_CRASHLOG_H_
#define ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_CRASHLOG_H_

#include <kernel/spinlock.h>
#include <platform/crashlog.h>

class EfiCrashlog final : public PlatformCrashlog::Interface {
 public:
  EfiCrashlog() = default;

  ktl::span<char> GetRenderTarget() final { return {render_target_, sizeof(render_target_)}; }
  void Finalize(zircon_crash_reason_t reason, size_t amt) final;
  size_t Recover(FILE* tgt) final;
  void EnableCrashlogUptimeUpdates(bool enabled) final {}

  void SetLastCrashlogLocation(ktl::string_view last_crashlog) {
    Guard<SpinLock, IrqSave> guard{&last_crashlog_lock_};
    last_crashlog_ = last_crashlog;
  }

 private:
  // Something big enough for the panic log but not too enormous
  // to avoid excessive pressure on efi variable storage
  static constexpr size_t kMaxEfiCrashlogLen = 4096;

  // Stashed crashlog-related values.
  DECLARE_SPINLOCK(EfiCrashlog) last_crashlog_lock_;
  ktl::string_view last_crashlog_ TA_GUARDED(last_crashlog_lock_){};
  char render_target_[kMaxEfiCrashlogLen];
};

#endif  // ZIRCON_KERNEL_INCLUDE_PLATFORM_EFI_CRASHLOG_H_
