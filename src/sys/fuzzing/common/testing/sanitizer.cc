// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <sanitizer/common_interface_defs.h>
#include <sanitizer/lsan_interface.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/sancov.h"
#include "src/sys/fuzzing/common/testing/module.h"

namespace fuzzing {

// The |FakeSanitizerRuntime| provides implementations of weak symbols usually provided by sanitizer
// runtime if no such runtime is present.
class FakeSanitizerRuntime final {
 public:
  FakeSanitizerRuntime() {
    __sanitizer_cov_8bit_counters_init(module_.counters(), module_.counters_end());
    __sanitizer_cov_pcs_init(module_.pcs(), module_.pcs_end());
  }

  ~FakeSanitizerRuntime() = default;

  uint8_t &operator[](size_t index) { return module_[index]; }

  using MallocHook = void (*)(const volatile void *, size_t);
  void set_malloc_hook(MallocHook malloc_hook) { malloc_hook_ = malloc_hook; }

  using DeathCallback = void (*)();
  void set_death_callback(DeathCallback death_callback) { death_callback_ = death_callback; }

  void set_leak(bool leak) { leak_ = leak; }

  int DoRecoverableLeakCheck() { return leak_ ? 1 : 0; }

  int AcquireCrashState() { return !crash_state_acquired_.exchange(true); }

  void OnMalloc(size_t len) {
    FX_CHECK(malloc_hook_) << "__sanitizer_install_malloc_and_free_hooks was not called.";
    malloc_hook_(this, len);
  }

  void OnDeath() {
    FX_CHECK(death_callback_) << "__sanitizer_set_death_callback was not called.";
    death_callback_();
    exit(1);
  }

 private:
  FakeModule module_;
  MallocHook malloc_hook_ = nullptr;
  DeathCallback death_callback_ = nullptr;
  bool leak_ = false;
  std::atomic<bool> crash_state_acquired_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeSanitizerRuntime);
};

// The fake sanitizer runtime needs to run code before |main| and the |InstrumentedProcess|
// constructor, so like the latter it takes an exception to usual rule requiring globally scoped
// objects to be trivially constructible. To mitigate the static initializtion order fiasco, it
// needs to be constructed as late as possible, but still ahead of |InstrumentedProcess|.
[[gnu::init_priority(0xfffe)]] FakeSanitizerRuntime gFakeSanitizerRuntime;

void SetCoverage(size_t index, uint8_t value) { gFakeSanitizerRuntime[index] = value; }

void Malloc(size_t size) { gFakeSanitizerRuntime.OnMalloc(size); }

void LeakMemory() { gFakeSanitizerRuntime.set_leak(true); }

void Die() { gFakeSanitizerRuntime.OnDeath(); }

}  // namespace fuzzing

extern "C" {

int __sanitizer_acquire_crash_state() { return fuzzing::gFakeSanitizerRuntime.AcquireCrashState(); }

void __sanitizer_print_memory_profile(size_t, size_t) {}

void __sanitizer_set_death_callback(void (*death_callback)(void)) {
  fuzzing::gFakeSanitizerRuntime.set_death_callback(death_callback);
}

int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void *, size_t),
                                              void (*free_hook)(const volatile void *)) {
  fuzzing::gFakeSanitizerRuntime.set_malloc_hook(malloc_hook);
  return 1;
}

void __sanitizer_purge_allocator() {}

void __lsan_enable() {}

void __lsan_disable() {}

int __lsan_do_recoverable_leak_check() {
  return fuzzing::gFakeSanitizerRuntime.DoRecoverableLeakCheck();
}

}  // extern "C"
